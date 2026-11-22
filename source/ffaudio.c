#include "ffaudio.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>

#include "tags_text.h"
#include "util.h"

#define AVIO_BUFFER (32 * 1024)
#define PROBE_BYTES (256 * 1024)     // MP4, Ogg and ADTS are known from their first bytes.
#define ANALYZE_US (AV_TIME_BASE / 2)
// A seek decodes from this much before the frame asked for: Opus wants
// 80 ms to settle, AAC one frame (its overlap) and a little more.
#define PREROLL_MS 100
#define SEEK_TRIES 4                 // Each one from a second further back.
#define MAX_DECODE_ERRORS 32         // Damaged packets in a row before giving up.
#define OGG_FIRST_PAGE 512           // Its header (27 bytes), up to 255 lacing values, the first packet's start.
#define OGG_PAGE_HEADER 27
#define FRONT_COVER 3
// Opus's R128 gains are to -23 LUFS; ReplayGain's reference is 5 dB louder.
#define R128_TO_REPLAYGAIN_DB 5.0
#define R128_UNITS_PER_DB 256.0

struct FfAudio {
    KFile *file;
    AVIOContext *avio;
    AVFormatContext *format;
    AVCodecContext *codec;
    AVPacket *packet;
    AVFrame *frame;
    int stream;
    bool flushed;              // The demuxer ended: the decoder was told.
    uint32_t channels, sample_rate;
    uint64_t total_frames;
    int64_t start_pts;         // The stream's start, in its time base.
    // The resampler, for the frames' format, layout and rate.
    SwrContext *swr;
    int swr_rate, swr_format;
    AVChannelLayout swr_layout;
    // The last frame, converted: interleaved float, channels per frame.
    float *pcm;
    size_t pcm_capacity, pcm_count, pcm_at;  // In frames.
    bool held;                 // a->frame is decoded, not yet converted (a seek looked at it).
    uint64_t skip;             // Frames still to drop: before a seek's target.
    // What Now Playing shows.
    char name[16];
    unsigned bits, kbps;
};

bool ffaudioIsFile(const char *path)
{
    return utilHasExtension(path, "m4a") || utilHasExtension(path, "mp4") || utilHasExtension(path, "aac") ||
           utilHasExtension(path, "opus");
}

// --- Reading through kfile ---

static int avioRead(void *opaque, uint8_t *buf, int size)
{
    KFile *f = opaque;
    size_t n = kfileRead(f, buf, (size_t)size);
    if (n > 0)
        return (int)n;
    return kfileFailed(f) ? AVERROR(EIO) : AVERROR_EOF;
}

static int64_t avioSeek(void *opaque, int64_t offset, int whence)
{
    KFile *f = opaque;
    whence &= ~AVSEEK_FORCE;
    if (whence == AVSEEK_SIZE) {
        int64_t size = kfileSize(f);
        return size >= 0 ? size : AVERROR(ENOSYS);
    }
    if ((whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) || !kfileSeek(f, offset, whence))
        return AVERROR(EINVAL);
    return kfileTell(f);
}

static void closeInput(AVIOContext **avio, AVFormatContext **format)
{
    avformat_close_input(format);
    if (*avio) {
        av_freep(&(*avio)->buffer);
        avio_context_free(avio);
    }
}

// The file's container, read from its start. False (both NULL) if FFmpeg doesn't know it.
static bool openInput(KFile *f, AVIOContext **avio, AVFormatContext **format)
{
    av_log_set_level(AV_LOG_QUIET);  // Nobody reads the console's stderr.
    *avio = NULL;
    *format = NULL;
    if (!kfileSeek(f, 0, SEEK_SET))
        return false;
    uint8_t *buffer = av_malloc(AVIO_BUFFER);
    *avio = buffer ? avio_alloc_context(buffer, AVIO_BUFFER, 0, f, avioRead, NULL, avioSeek) : NULL;
    AVFormatContext *c = *avio ? avformat_alloc_context() : NULL;
    if (!c) {
        if (!*avio)
            av_free(buffer);
        closeInput(avio, format);
        return false;
    }
    c->pb = *avio;
    c->flags |= AVFMT_FLAG_CUSTOM_IO;
    c->probesize = PROBE_BYTES;
    c->max_analyze_duration = ANALYZE_US;
    if (avformat_open_input(&c, NULL, NULL, NULL) != 0) {  // It frees c.
        closeInput(avio, format);
        return false;
    }
    *format = c;
    return true;
}

// --- Opening ---

static void describe(FfAudio *a)
{
    const AVStream *st = a->format->streams[a->stream];
    const AVCodecContext *c = a->codec;
    const char *name = "Opus";
    if (c->codec_id == AV_CODEC_ID_ALAC)
        name = "ALAC";
    else if (c->codec_id == AV_CODEC_ID_AAC)
        name = c->profile == AV_PROFILE_AAC_HE || c->profile == AV_PROFILE_AAC_HE_V2 ? "HE-AAC" : "AAC";
    snprintf(a->name, sizeof(a->name), "%s", name);
    if (c->codec_id == AV_CODEC_ID_ALAC) {
        a->bits = c->bits_per_raw_sample ? (unsigned)c->bits_per_raw_sample : (unsigned)st->codecpar->bits_per_raw_sample;
        return;
    }
    // Lossy: the stream's rate, or (Ogg doesn't say) the file's size over its
    // length, without the cover (in base64 in an Ogg file's comments).
    int64_t rate = st->codecpar->bit_rate;
    double seconds = a->sample_rate ? (double)a->total_frames / a->sample_rate : 0.0;
    if (rate <= 0 && seconds > 0.0) {
        bool base64 = strcmp(a->format->iformat->name, "ogg") == 0;
        int64_t bytes = kfileSize(a->file);
        for (unsigned i = 0; i < a->format->nb_streams; i++)
            bytes -= (int64_t)a->format->streams[i]->attached_pic.size * (base64 ? 4 : 3) / 3;
        rate = bytes > 0 ? (int64_t)(bytes * 8.0 / seconds) : 0;
    }
    a->kbps = rate > 0 ? (unsigned)((rate + 500) / 1000) : 0;
}

static uint64_t streamFrames(const FfAudio *a)
{
    const AVStream *st = a->format->streams[a->stream];
    AVRational per_frame = {1, (int)a->sample_rate};
    int64_t frames = 0;
    if (st->duration != AV_NOPTS_VALUE)
        frames = av_rescale_q(st->duration, st->time_base, per_frame);
    else if (a->format->duration != AV_NOPTS_VALUE)
        frames = av_rescale_q(a->format->duration, AV_TIME_BASE_Q, per_frame);
    return frames > 0 ? (uint64_t)frames : 0;
}

static bool openCodec(FfAudio *a)
{
    const AVCodec *codec = NULL;
    a->stream = av_find_best_stream(a->format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (a->stream < 0 || !codec)
        return false;
    AVStream *st = a->format->streams[a->stream];
    for (unsigned i = 0; i < a->format->nb_streams; i++)  // A cover or a video: not read while playing.
        if ((int)i != a->stream)
            a->format->streams[i]->discard = AVDISCARD_ALL;
    a->codec = avcodec_alloc_context3(codec);
    if (!a->codec || avcodec_parameters_to_context(a->codec, st->codecpar) < 0)
        return false;
    a->codec->pkt_timebase = st->time_base;
    a->codec->thread_count = 1;
    if (avcodec_open2(a->codec, codec, NULL) != 0 || a->codec->sample_rate <= 0)
        return false;
    a->sample_rate = (uint32_t)a->codec->sample_rate;
    a->channels = a->codec->ch_layout.nb_channels == 1 ? 1 : 2;
    a->start_pts = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    a->total_frames = streamFrames(a);
    describe(a);
    return true;
}

FfAudio *ffaudioOpen(KFile *f)
{
    FfAudio *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->file = f;
    a->stream = -1;
    a->packet = av_packet_alloc();
    a->frame = av_frame_alloc();
    bool ok = a->packet && a->frame && openInput(f, &a->avio, &a->format) &&
              avformat_find_stream_info(a->format, NULL) >= 0 && openCodec(a);
    if (!ok) {
        ffaudioClose(a);
        return NULL;
    }
    return a;
}

void ffaudioClose(FfAudio *a)
{
    if (!a)
        return;
    swr_free(&a->swr);
    av_channel_layout_uninit(&a->swr_layout);
    avcodec_free_context(&a->codec);
    closeInput(&a->avio, &a->format);
    av_packet_free(&a->packet);
    av_frame_free(&a->frame);
    free(a->pcm);
    free(a);
}

uint32_t ffaudioChannels(const FfAudio *a)
{
    return a->channels;
}

uint32_t ffaudioSampleRate(const FfAudio *a)
{
    return a->sample_rate;
}

uint64_t ffaudioTotalFrames(const FfAudio *a)
{
    return a->total_frames;
}

void ffaudioFormat(const FfAudio *a, AudioFormat *out)
{
    *out = (AudioFormat){.sample_rate = a->sample_rate, .bits = a->bits, .kbps = a->kbps};
    snprintf(out->codec, sizeof(out->codec), "%s", a->name);
}

// --- Decoding ---

// The next packet of the stream to the decoder. False at the end of the file.
static bool sendPacket(FfAudio *a)
{
    while (av_read_frame(a->format, a->packet) >= 0) {
        bool ours = a->packet->stream_index == a->stream;
        if (ours)
            avcodec_send_packet(a->codec, a->packet);  // A damaged packet is skipped.
        av_packet_unref(a->packet);
        if (ours)
            return true;
    }
    return false;
}

// The next decoded frame, in a->frame. False at the end.
static bool nextFrame(FfAudio *a)
{
    int errors = 0;
    while (errors < MAX_DECODE_ERRORS) {
        int rc = avcodec_receive_frame(a->codec, a->frame);
        if (rc == 0)
            return true;
        if (rc == AVERROR_EOF)
            return false;
        if (rc != AVERROR(EAGAIN)) {
            errors++;
            continue;
        }
        if (a->flushed)
            return false;
        if (!sendPacket(a)) {
            avcodec_send_packet(a->codec, NULL);  // The decoder gives what it still holds.
            a->flushed = true;
        }
    }
    return false;
}

// Makes the resampler match the frame (HE-AAC can change after the first
// frames). The output stays the file's: its rate, mono or stereo, float.
static bool prepareResampler(FfAudio *a, const AVFrame *f)
{
    if (a->swr && f->sample_rate == a->swr_rate && f->format == a->swr_format &&
        av_channel_layout_compare(&f->ch_layout, &a->swr_layout) == 0)
        return true;
    swr_free(&a->swr);
    av_channel_layout_uninit(&a->swr_layout);
    AVChannelLayout in = {0}, out = {0};
    if (f->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&in, f->ch_layout.nb_channels);
    else if (av_channel_layout_copy(&in, &f->ch_layout) < 0)
        return false;
    av_channel_layout_default(&out, (int)a->channels);
    bool ok = swr_alloc_set_opts2(&a->swr, &out, AV_SAMPLE_FMT_FLT, (int)a->sample_rate, &in, f->format,
                                  f->sample_rate, 0, NULL) >= 0 &&
              swr_init(a->swr) >= 0;
    av_channel_layout_uninit(&in);
    av_channel_layout_uninit(&out);
    if (!ok) {
        swr_free(&a->swr);
        return false;
    }
    a->swr_rate = f->sample_rate;
    a->swr_format = f->format;
    return av_channel_layout_copy(&a->swr_layout, &f->ch_layout) >= 0;
}

static bool reserve(FfAudio *a, size_t frames)
{
    if (frames <= a->pcm_capacity)
        return true;
    float *grown = realloc(a->pcm, frames * a->channels * sizeof(float));
    if (!grown)
        return false;
    a->pcm = grown;
    a->pcm_capacity = frames;
    return true;
}

// Where a frame starts, in frames of the file's rate. Before 0 for what a
// decoder drops at the start (Opus's pre-skip, AAC's priming), which after a
// seek it doesn't. False if the frame has no time.
static bool framePosition(const FfAudio *a, const AVFrame *f, int64_t *at)
{
    int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    if (ts == AV_NOPTS_VALUE)
        return false;
    *at = av_rescale_q(ts - a->start_pts, a->format->streams[a->stream]->time_base,
                       (AVRational){1, (int)a->sample_rate});
    return true;
}

// Decodes and converts the next frame into pcm. False at the end or on an error.
static bool refill(FfAudio *a)
{
    if (!a->held && !nextFrame(a))
        return false;
    a->held = false;
    bool ok = prepareResampler(a, a->frame);
    int room = ok ? swr_get_out_samples(a->swr, a->frame->nb_samples) : -1;
    ok = room >= 0 && reserve(a, (size_t)room + 1);
    uint8_t *to[1] = {(uint8_t *)a->pcm};
    int n = ok ? swr_convert(a->swr, to, room, (const uint8_t **)a->frame->extended_data, a->frame->nb_samples) : -1;
    av_frame_unref(a->frame);
    if (n < 0)
        return false;
    size_t drop = a->skip < (uint64_t)n ? (size_t)a->skip : (size_t)n;
    a->skip -= drop;
    a->pcm_count = (size_t)n;
    a->pcm_at = drop;
    return true;
}

uint64_t ffaudioRead(FfAudio *a, float *out, uint64_t frames)
{
    uint64_t done = 0;
    while (done < frames) {
        if (a->pcm_at == a->pcm_count) {
            if (!refill(a))
                break;
            continue;
        }
        size_t n = a->pcm_count - a->pcm_at;
        if (n > frames - done)
            n = (size_t)(frames - done);
        memcpy(out + done * a->channels, a->pcm + a->pcm_at * a->channels, n * a->channels * sizeof(float));
        a->pcm_at += n;
        done += n;
    }
    return done;
}

// The decoder at ts (in the stream's time base), its first frame decoded. False if it can't.
static bool seekTo(FfAudio *a, int64_t ts)
{
    if (av_seek_frame(a->format, a->stream, ts, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(a->codec);
    swr_free(&a->swr);  // Nothing of before the seek comes out.
    a->flushed = false;
    a->pcm_count = a->pcm_at = 0;
    a->skip = 0;
    a->held = nextFrame(a);
    return true;
}

bool ffaudioSeek(FfAudio *a, uint64_t frame)
{
    AVRational per_frame = {1, (int)a->sample_rate}, base = a->format->streams[a->stream]->time_base;
    // Near the start, from before 0: the packets a decoder drops come first.
    int64_t from = (int64_t)frame - (int64_t)a->sample_rate * PREROLL_MS / 1000;
    for (int attempt = 0; attempt < SEEK_TRIES; attempt++) {
        bool sought = seekTo(a, av_rescale_q(from, per_frame, base) + a->start_pts);
        if (!sought && from < 0) {  // A demuxer that won't go before the start (ADTS).
            from = 0;
            sought = seekTo(a, a->start_pts);
        }
        if (!sought)
            return false;
        int64_t at = 0;
        if (!a->held || !framePosition(a, a->frame, &at))
            return true;  // Past the end, or no times: where it landed.
        if (at <= (int64_t)frame) {
            a->skip = (uint64_t)((int64_t)frame - at);
            return true;
        }
        from -= (int64_t)a->sample_rate;  // It landed after the frame: from further back.
    }
    return true;
}

// --- Tags ---

// One FFmpeg metadata entry, as the Vorbis comment it stands for.
static void applyEntry(const char *key, const char *value, TrackTags *t)
{
    static const struct {
        const char *ffmpeg, *comment;
    } NAMES[] = {
        {"album_artist", "ALBUMARTIST"},
        {"track", "TRACKNUMBER"},
        {"disc", "DISCNUMBER"},
    };
    bool track = utilCompareNames(key, "R128_TRACK_GAIN") == 0;
    if (track || utilCompareNames(key, "R128_ALBUM_GAIN") == 0) {
        char *end;
        long units = strtol(value, &end, 10);
        if (end == value)
            return;
        char db[32];
        snprintf(db, sizeof(db), "%.2f", units / R128_UNITS_PER_DB + R128_TO_REPLAYGAIN_DB);
        tagsApplyReplayGain(track ? "REPLAYGAIN_TRACK_GAIN" : "REPLAYGAIN_ALBUM_GAIN", db, t);
        return;
    }
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++)
        if (utilCompareNames(key, NAMES[i].ffmpeg) == 0)
            key = NAMES[i].comment;
    char comment[TAGS_TEXT_MAX + 64];
    int n = snprintf(comment, sizeof(comment), "%s=%s", key, value);
    if (n > 0)
        tagsApplyComment(comment, (size_t)n < sizeof(comment) ? (size_t)n : sizeof(comment) - 1, t);
}

static void applyMetadata(const AVDictionary *m, TrackTags *t)
{
    const AVDictionaryEntry *e = NULL;
    while ((e = av_dict_iterate(m, e)) != NULL)
        applyEntry(e->key, e->value, t);
}

// A picture's kind: FFmpeg names the ID3/FLAC types in its "comment"; an MP4 cover has none.
static int pictureType(const AVStream *st)
{
    const AVDictionaryEntry *e = av_dict_get(st->metadata, "comment", NULL, 0);
    return !e || strcmp(e->value, "Cover (front)") == 0 ? FRONT_COVER : 0;
}

bool ffaudioReadTags(KFile *f, TrackTags *t)
{
    AVIOContext *avio;
    AVFormatContext *format;
    if (!openInput(f, &avio, &format))
        return false;
    applyMetadata(format->metadata, t);
    for (unsigned i = 0; i < format->nb_streams; i++) {
        const AVStream *st = format->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            applyMetadata(st->metadata, t);  // Ogg keeps its comments with the stream.
        if ((st->disposition & AV_DISPOSITION_ATTACHED_PIC) && st->attached_pic.size > 0)
            textOfferPicture(t, st->attached_pic.data, (size_t)st->attached_pic.size, pictureType(st));
    }
    closeInput(&avio, &format);
    return true;
}

bool ffaudioOggIsOpus(KFile *f)
{
    uint8_t page[OGG_FIRST_PAGE];
    size_t n = kfileSeek(f, 0, SEEK_SET) ? kfileRead(f, page, sizeof(page)) : 0;
    kfileSeek(f, 0, SEEK_SET);
    // The first page holds the first packet alone, right after its lacing values.
    if (n <= OGG_PAGE_HEADER || memcmp(page, "OggS", 4) != 0)
        return false;
    size_t at = OGG_PAGE_HEADER + page[OGG_PAGE_HEADER - 1];
    return n >= at + 8 && memcmp(page + at, "OpusHead", 8) == 0;
}
