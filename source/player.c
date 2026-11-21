#include "player.h"

#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "decoder.h"
#include "kfile.h"
#include "lang.h"
#include "resampler.h"
#include "tags.h"
#include "util.h"

#define BUFFER_COUNT 3
#define BUFFER_FRAMES 2048
#define BUFFER_BYTES (BUFFER_FRAMES * 2 * sizeof(s16))
#define BUFFER_ALIGN 0x1000
#define WAIT_TIMEOUT_NS 20000000ull
#define IDLE_SLEEP_NS 10000000ull
#define RESTART_THRESHOLD_SECONDS 3.0
#define MAX_OPEN_FAILURES 32
#define MAX_NETWORK_FAILURES 3  // Network songs that fail in a row: the server is gone, not those songs.
#define THREAD_STACK_SIZE 0x40000  // Roomy: a network folder's reads (libsmb2, curl) run on it.
#define THREAD_PRIORITY 0x2B

// Shared with the UI thread: guarded by lock.
static Mutex g_lock;
static TrackList g_list;
static Queue g_queue;
static PlayerState g_state;
static bool g_shuffle;
static RepeatMode g_repeat;
static bool g_load_pending;
static double g_seek_target = -1.0;
static u64 g_played_frames;
static double g_duration;
static AudioFormat g_format;  // Of the file the decoder plays.
static char g_error[320];
static unsigned g_generation;
static double g_seeking_to = -1.0;  // A seek the audio thread is running.
static bool g_eq_enabled;
static int g_eq_gains[EQ_BANDS];
static bool g_eq_changed = true;
static ReplayGainMode g_replaygain;
static bool g_mixed_albums;

// Owned by the audio thread: the equalizer and the current track's gain.
static Eq g_eq;
static float g_track_gain = 1.0f;
static u64 g_fade_total;  // Frames of the running fade-out, 0 if none.
static u64 g_fade_left;

// Owned by the audio thread.
static Thread g_thread;
static volatile bool g_running;
static Decoder *g_decoder;
static Resampler g_resampler;
static u32 g_out_rate;
static AudioOutBuffer g_buffers[BUFFER_COUNT];
static bool g_queued[BUFFER_COUNT];
static float g_mix[BUFFER_FRAMES * 2];

static u64 readDecoder(void *ctx, float *out, u64 frames)
{
    return decoderReadStereo((Decoder *)ctx, out, frames);
}

static void installDecoder(Decoder *d)
{
    decoderClose(g_decoder);
    g_decoder = d;
    if (d)
        resamplerReset(&g_resampler, decoderSampleRate(d), g_out_rate);
}

// The ReplayGain of a file as a linear factor, for the chosen mode. Album
// mode falls back to the track gain; files without tags play as they are.
static float replayGainFor(const char *path)
{
    mutexLock(&g_lock);
    ReplayGainMode mode = g_replaygain;
    bool shuffle = g_shuffle, mixed = g_mixed_albums;
    mutexUnlock(&g_lock);
    if (mode == REPLAYGAIN_OFF)
        return 1.0f;
    TrackTags tags;
    tagsInit(&tags);
    tags.skip_picture = true;
    tagsRead(path, &tags);
    float db = replayGainDb(mode, shuffle, mixed, &tags);
    tagsFree(&tags);
    return replayGainFactor(db);
}

// Opens the queue's current track, skipping files that fail to open.
// Returns true if the track that was current on entry is now loaded.
static bool loadCurrent(void)
{
    char path[UTIL_PATH_MAX];
    int network_failures = 0;
    for (int attempt = 0; attempt < MAX_OPEN_FAILURES; attempt++) {
        mutexLock(&g_lock);
        if (g_list.count == 0) {
            mutexUnlock(&g_lock);
            installDecoder(NULL);
            return false;
        }
        snprintf(path, sizeof(path), "%s", g_list.paths[queueCurrent(&g_queue)]);
        mutexUnlock(&g_lock);

        Decoder *d = decoderOpen(path);
        if (d)
            g_track_gain = replayGainFor(path);

        mutexLock(&g_lock);
        if (d) {
            g_played_frames = 0;
            u64 total = decoderTotalFrames(d);
            g_duration = total ? (double)total / decoderSampleRate(d) : 0.0;
            decoderFormat(d, &g_format);
            bool requested = attempt == 0 && !g_load_pending;
            mutexUnlock(&g_lock);
            installDecoder(d);
            return requested;
        }
        if (g_load_pending) {
            // The UI changed the queue meanwhile; handleCommands loads its pick.
            mutexUnlock(&g_lock);
            return false;
        }
        snprintf(g_error, sizeof(g_error), T("Could not open: %s"), utilFileName(path));
        if (kfileIsRemote(path) && ++network_failures >= MAX_NETWORK_FAILURES) {
            mutexUnlock(&g_lock);
            break;  // Without waiting for each of the others in turn.
        }
        queueNext(&g_queue, true);
        mutexUnlock(&g_lock);
    }

    installDecoder(NULL);
    mutexLock(&g_lock);
    g_state = PLAYER_STOPPED;
    g_fade_total = g_fade_left = 0;
    mutexUnlock(&g_lock);
    return false;
}

static void stopPlayback(void)
{
    mutexLock(&g_lock);
    g_state = PLAYER_STOPPED;
    g_fade_total = g_fade_left = 0;
    mutexUnlock(&g_lock);
    installDecoder(NULL);
}

// Called at the end of a track. Returns false when filling should stop.
static bool advanceAfterTrack(void)
{
    mutexLock(&g_lock);
    if (g_load_pending) {
        // A user skip already moved the queue; handleCommands loads it.
        mutexUnlock(&g_lock);
        return false;
    }
    bool more = queueNext(&g_queue, false);
    if (!more) {
        g_state = PLAYER_STOPPED;
        g_fade_total = g_fade_left = 0;
        g_queue.pos = 0;
        g_played_frames = 0;
    }
    mutexUnlock(&g_lock);

    if (more)
        loadCurrent();
    else
        installDecoder(NULL);
    return more && g_decoder;
}

// Full scale: the console's volume, set with its buttons, is the only volume.
// gain_start..gain_end ramps linearly over the buffer, for the fade-out.
static void writeSamples(s16 *dst, const float *src, u64 frames, float gain_start, float gain_end)
{
    float step = frames ? (gain_end - gain_start) / (float)frames : 0.0f;
    for (u64 i = 0; i < frames * 2; i++) {
        float v = src[i] * (gain_start + step * (float)(i / 2));
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        dst[i] = (s16)(v * 32767.0f);
    }
}

// Takes new equalizer settings from the UI thread.
static void updateEqualizer(void)
{
    int gains[EQ_BANDS];
    mutexLock(&g_lock);
    bool changed = g_eq_changed;
    for (int b = 0; b < EQ_BANDS; b++)
        gains[b] = g_eq_enabled ? g_eq_gains[b] : 0;
    g_eq_changed = false;
    mutexUnlock(&g_lock);
    if (changed)
        eqConfigure(&g_eq, gains, g_out_rate);
}

// Decodes the next block into a buffer. False if there is nothing to play.
static bool fillBuffer(AudioOutBuffer *buffer, float gain_start, float gain_end)
{
    u64 done = 0;
    int empty_tracks = 0;
    float track_gain = g_track_gain;  // The gain of the song this buffer starts in.
    while (done < BUFFER_FRAMES && g_decoder && g_running) {
        u64 n = resamplerRun(&g_resampler, readDecoder, g_decoder,
                             g_mix + done * 2, BUFFER_FRAMES - done);
        done += n;
        mutexLock(&g_lock);
        g_played_frames += n;
        mutexUnlock(&g_lock);
        if (done == BUFFER_FRAMES)
            break;
        // Tracks that decode to nothing would otherwise loop forever with repeat on.
        empty_tracks = n == 0 ? empty_tracks + 1 : 0;
        if (empty_tracks > MAX_OPEN_FAILURES) {
            stopPlayback();
            break;
        }
        if (!advanceAfterTrack())
            break;
    }
    if (done == 0)
        return false;

    memset(g_mix + done * 2, 0, (BUFFER_FRAMES - done) * 2 * sizeof(float));
    updateEqualizer();
    eqProcess(&g_eq, g_mix, BUFFER_FRAMES);
    writeSamples(buffer->buffer, g_mix, BUFFER_FRAMES, gain_start * track_gain, gain_end * track_gain);
    buffer->data_size = BUFFER_BYTES;
    buffer->data_offset = 0;
    return true;
}

static void applySeek(double seconds)
{
    bool ok = false;
    if (g_decoder && decoderSeek(g_decoder, (u64)(seconds * decoderSampleRate(g_decoder)))) {
        resamplerReset(&g_resampler, decoderSampleRate(g_decoder), g_out_rate);
        ok = true;
    }
    mutexLock(&g_lock);
    if (ok)
        g_played_frames = (u64)(seconds * g_out_rate);
    g_seeking_to = -1.0;
    mutexUnlock(&g_lock);
}

static void handleCommands(void)
{
    mutexLock(&g_lock);
    bool load = g_load_pending;
    double seek = g_seek_target;
    g_load_pending = false;
    g_seek_target = -1.0;
    g_seeking_to = seek;  // Still reported as the position until it's done.
    mutexUnlock(&g_lock);

    // Every command that loads a track clears the seek target, except a
    // restore, which loads and then seeks to the saved position.
    // A restore's seek belongs to the track it loads: drop it if that track
    // failed and another was loaded, or the UI asked for a different one.
    if (load && !loadCurrent())
        seek = -1.0;
    if (seek >= 0.0) {
        applySeek(seek);
    } else {
        mutexLock(&g_lock);
        g_seeking_to = -1.0;
        mutexUnlock(&g_lock);
    }
}

// Moves the fade-out on by one buffer; at silence, pauses.
static void advanceFade(void)
{
    mutexLock(&g_lock);
    if (g_fade_total) {
        g_fade_left = g_fade_left > BUFFER_FRAMES ? g_fade_left - BUFFER_FRAMES : 0;
        if (g_fade_left == 0) {
            g_fade_total = 0;
            if (g_state == PLAYER_PLAYING)
                g_state = PLAYER_PAUSED;
        }
    }
    mutexUnlock(&g_lock);
}

static void submitBuffers(void)
{
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (g_queued[i])
            continue;
        mutexLock(&g_lock);
        bool playing = g_state == PLAYER_PLAYING;
        float gain_start = 1.0f, gain_end = 1.0f;
        if (g_fade_total) {
            u64 after = g_fade_left > BUFFER_FRAMES ? g_fade_left - BUFFER_FRAMES : 0;
            gain_start = (float)g_fade_left / (float)g_fade_total;
            gain_end = (float)after / (float)g_fade_total;
        }
        mutexUnlock(&g_lock);
        if (!playing || !fillBuffer(&g_buffers[i], gain_start, gain_end))
            return;
        if (R_SUCCEEDED(audoutAppendAudioOutBuffer(&g_buffers[i])))
            g_queued[i] = true;
        advanceFade();
    }
}

static void reapBuffers(void)
{
    bool any_queued = false;
    for (int i = 0; i < BUFFER_COUNT; i++)
        any_queued |= g_queued[i];
    if (!any_queued) {
        svcSleepThread(IDLE_SLEEP_NS);
        return;
    }

    AudioOutBuffer *released = NULL;
    u32 count = 0;
    if (R_FAILED(audoutWaitPlayFinish(&released, &count, WAIT_TIMEOUT_NS)) || count == 0)
        return;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (released == &g_buffers[i])
            g_queued[i] = false;
    }
}

static void audioThreadMain(void *arg)
{
    (void)arg;
    while (g_running) {
        handleCommands();
        submitBuffers();
        reapBuffers();
    }
}

static bool allocateBuffers(void)
{
    for (int i = 0; i < BUFFER_COUNT; i++) {
        void *memory = memalign(BUFFER_ALIGN, BUFFER_BYTES);
        if (!memory)
            return false;
        memset(memory, 0, BUFFER_BYTES);
        g_buffers[i] = (AudioOutBuffer){
            .next = NULL,
            .buffer = memory,
            .buffer_size = BUFFER_BYTES,
            .data_size = BUFFER_BYTES,
            .data_offset = 0,
        };
    }
    return true;
}

static void freeBuffers(void)
{
    for (int i = 0; i < BUFFER_COUNT; i++) {
        free(g_buffers[i].buffer);
        g_buffers[i].buffer = NULL;
    }
}

bool playerInit(bool shuffle, RepeatMode repeat)
{
    mutexInit(&g_lock);
    g_shuffle = shuffle;
    g_repeat = repeat;
    g_state = PLAYER_STOPPED;
    g_fade_total = g_fade_left = 0;

    if (R_FAILED(audoutInitialize()))
        return false;
    if (audoutGetChannelCount() != 2 || audoutGetPcmFormat() != PcmFormat_Int16 ||
        !allocateBuffers() || R_FAILED(audoutStartAudioOut())) {
        freeBuffers();
        audoutExit();
        return false;
    }
    g_out_rate = audoutGetSampleRate();

    g_running = true;
    if (R_FAILED(threadCreate(&g_thread, audioThreadMain, NULL, NULL,
                              THREAD_STACK_SIZE, THREAD_PRIORITY, -2)) ||
        R_FAILED(threadStart(&g_thread))) {
        g_running = false;
        audoutStopAudioOut();
        freeBuffers();
        audoutExit();
        return false;
    }
    return true;
}

void playerExit(void)
{
    g_running = false;
    kfileInterruptStreams();
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    audoutStopAudioOut();
    bool flushed;
    audoutFlushAudioOutBuffers(&flushed);
    freeBuffers();
    audoutExit();
    installDecoder(NULL);
    queueFree(&g_queue);
    trackListFree(&g_list);
}

static void cancelFadeLocked(void)
{
    g_fade_total = 0;
    g_fade_left = 0;
}

// Replaces the list and starts at start. Call with the lock held.
static void startListLocked(TrackList *list, size_t start)
{
    kfileInterruptStreams();  // The song playing from the network needn't wait for it.
    cancelFadeLocked();
    trackListFree(&g_list);
    queueFree(&g_queue);
    g_list = *list;
    *list = (TrackList){0};
    g_generation++;
    if (queueInit(&g_queue, g_list.count, start, g_shuffle, armGetSystemTick())) {
        g_queue.repeat = g_repeat;
        g_state = PLAYER_PLAYING;
        g_load_pending = true;
        g_seek_target = -1.0;
        g_played_frames = 0;
        g_duration = 0.0;
        g_error[0] = '\0';
    } else {
        g_state = PLAYER_STOPPED;
        g_fade_total = g_fade_left = 0;
    }
}

void playerPlay(TrackList *list, size_t start)
{
    mutexLock(&g_lock);
    startListLocked(list, start);
    mutexUnlock(&g_lock);
}

void playerRestore(TrackList *list, size_t track, double position)
{
    mutexLock(&g_lock);
    startListLocked(list, track);
    if (g_state == PLAYER_PLAYING) {
        g_state = PLAYER_PAUSED;
        g_seek_target = position > 0.0 ? position : -1.0;
    }
    mutexUnlock(&g_lock);
}

bool playerSnapshot(TrackList *out, size_t *track, double *position)
{
    mutexLock(&g_lock);
    bool ok = g_list.count > 0 && trackListCopy(out, &g_list);
    if (ok) {
        *track = queueCurrent(&g_queue);
        // Right after a restore the seek may not have happened yet.
        *position = g_seek_target >= 0.0 ? g_seek_target
                  : g_out_rate ? (double)g_played_frames / g_out_rate : 0.0;
    }
    mutexUnlock(&g_lock);
    return ok;
}

// Loads the queue's current track and plays it. Call with the lock held.
static void startCurrentLocked(void)
{
    if (g_list.count == 0)
        return;
    kfileInterruptStreams();  // The song playing from the network needn't wait for it.
    // A running fade (the sleep timer) continues across skips; only a
    // pause/play or a new list cancels it.
    g_load_pending = true;
    g_seek_target = -1.0;
    g_played_frames = 0;
    g_duration = 0.0;
    g_state = PLAYER_PLAYING;
}

void playerTogglePause(void)
{
    mutexLock(&g_lock);
    cancelFadeLocked();
    if (g_state == PLAYER_PLAYING)
        g_state = PLAYER_PAUSED;
    else if (g_state == PLAYER_PAUSED)
        g_state = PLAYER_PLAYING;
    else
        startCurrentLocked();
    mutexUnlock(&g_lock);
}

void playerNext(void)
{
    mutexLock(&g_lock);
    if (queueNext(&g_queue, true))
        startCurrentLocked();
    mutexUnlock(&g_lock);
}

void playerPrev(void)
{
    mutexLock(&g_lock);
    double position = g_out_rate ? (double)g_played_frames / g_out_rate : 0.0;
    if (g_state != PLAYER_STOPPED && position > RESTART_THRESHOLD_SECONDS) {
        g_seek_target = 0.0;
    } else {
        queuePrev(&g_queue);
        startCurrentLocked();
    }
    mutexUnlock(&g_lock);
}

void playerSeek(double delta_seconds)
{
    mutexLock(&g_lock);
    if (g_state != PLAYER_STOPPED && g_out_rate) {
        double base = g_seek_target >= 0.0 ? g_seek_target : (double)g_played_frames / g_out_rate;
        double target = base + delta_seconds;
        if (g_duration > 0.0 && target > g_duration - 1.0)
            target = g_duration - 1.0;
        g_seek_target = target < 0.0 ? 0.0 : target;
    }
    mutexUnlock(&g_lock);
}

void playerSetEqualizer(bool enabled, const int gains[EQ_BANDS])
{
    mutexLock(&g_lock);
    g_eq_enabled = enabled;
    memcpy(g_eq_gains, gains, sizeof(g_eq_gains));
    g_eq_changed = true;
    mutexUnlock(&g_lock);
}

void playerSetReplayGain(ReplayGainMode mode)
{
    mutexLock(&g_lock);
    g_replaygain = mode;
    mutexUnlock(&g_lock);
}

void playerSetMixedAlbums(bool mixed)
{
    mutexLock(&g_lock);
    g_mixed_albums = mixed;
    mutexUnlock(&g_lock);
}

void playerSeekTo(double seconds)
{
    mutexLock(&g_lock);
    if (g_state != PLAYER_STOPPED) {
        if (g_duration > 0.0 && seconds > g_duration - 1.0)
            seconds = g_duration - 1.0;
        g_seek_target = seconds < 0.0 ? 0.0 : seconds;
    }
    mutexUnlock(&g_lock);
}

void playerFadeOutAndPause(double seconds)
{
    mutexLock(&g_lock);
    if (g_state == PLAYER_PLAYING && !g_fade_total) {
        // No rate yet (a track still loading) or no time: pause at once.
        u64 frames = seconds > 0.0 && g_out_rate ? (u64)(seconds * g_out_rate) : 0;
        if (frames == 0) {
            g_state = PLAYER_PAUSED;
        } else {
            g_fade_total = frames;
            g_fade_left = frames;
        }
    }
    mutexUnlock(&g_lock);
}

void playerJumpTo(size_t track)
{
    mutexLock(&g_lock);
    if (track < g_list.count) {
        queueJumpTo(&g_queue, track);
        startCurrentLocked();
    }
    mutexUnlock(&g_lock);
}

void playerToggleShuffle(void)
{
    mutexLock(&g_lock);
    g_shuffle = !g_shuffle;
    queueSetShuffle(&g_queue, g_shuffle);
    mutexUnlock(&g_lock);
}

void playerSetShuffle(bool shuffle)
{
    mutexLock(&g_lock);
    if (g_shuffle != shuffle) {
        g_shuffle = shuffle;
        queueSetShuffle(&g_queue, shuffle);
    }
    mutexUnlock(&g_lock);
}

void playerCycleRepeat(void)
{
    mutexLock(&g_lock);
    g_repeat = (RepeatMode)((g_repeat + 1) % REPEAT_MODE_COUNT);
    g_queue.repeat = g_repeat;
    mutexUnlock(&g_lock);
}

void playerGetStatus(PlayerStatus *out)
{
    mutexLock(&g_lock);
    out->state = g_state;
    out->track = queueCurrent(&g_queue);
    out->queue_pos = g_queue.pos;
    out->generation = g_generation;
    out->fading = g_fade_total != 0 && g_state == PLAYER_PLAYING;
    out->track_count = g_list.count;
    // A seek that is asked for or running shows its target, so the progress
    // bar doesn't flash back to the old position meanwhile.
    out->position = g_seek_target >= 0.0 ? g_seek_target
                  : g_seeking_to >= 0.0  ? g_seeking_to
                  : g_out_rate ? (double)g_played_frames / g_out_rate : 0.0;
    out->duration = g_duration;
    out->shuffle = g_shuffle;
    out->repeat = g_repeat;
    out->format = g_decoder ? g_format : (AudioFormat){0};
    snprintf(out->error, sizeof(out->error), "%s", g_error);
    mutexUnlock(&g_lock);
}

bool playerQueueTrack(size_t pos, size_t *track)
{
    mutexLock(&g_lock);
    bool ok = pos < g_queue.count;
    if (ok)
        *track = g_queue.order[pos];
    mutexUnlock(&g_lock);
    return ok;
}

bool playerTrackPath(size_t track, char *out, size_t size)
{
    mutexLock(&g_lock);
    bool ok = track < g_list.count;
    if (ok)
        snprintf(out, size, "%s", g_list.paths[track]);
    mutexUnlock(&g_lock);
    return ok;
}
