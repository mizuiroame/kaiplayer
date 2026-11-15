#include "player.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "decoder.h"
#include "resampler.h"
#include "util.h"

#define BUFFER_COUNT 3
#define BUFFER_FRAMES 2048
#define BUFFER_BYTES (BUFFER_FRAMES * 2 * sizeof(s16))
#define BUFFER_ALIGN 0x1000
#define WAIT_TIMEOUT_NS 20000000ull
#define IDLE_SLEEP_NS 10000000ull
#define RESTART_THRESHOLD_SECONDS 3.0
#define MAX_OPEN_FAILURES 32
#define THREAD_STACK_SIZE 0x8000
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
static char g_error[160];

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

// Opens the queue's current track, skipping files that fail to open.
static void loadCurrent(void)
{
    char path[UTIL_PATH_MAX];
    for (int attempt = 0; attempt < MAX_OPEN_FAILURES; attempt++) {
        mutexLock(&g_lock);
        if (g_list.count == 0) {
            mutexUnlock(&g_lock);
            installDecoder(NULL);
            return;
        }
        snprintf(path, sizeof(path), "%s", g_list.paths[queueCurrent(&g_queue)]);
        mutexUnlock(&g_lock);

        Decoder *d = decoderOpen(path);

        mutexLock(&g_lock);
        if (d) {
            g_played_frames = 0;
            u64 total = decoderTotalFrames(d);
            g_duration = total ? (double)total / decoderSampleRate(d) : 0.0;
            mutexUnlock(&g_lock);
            installDecoder(d);
            return;
        }
        if (g_load_pending) {
            // The UI changed the queue meanwhile; handleCommands loads its pick.
            mutexUnlock(&g_lock);
            return;
        }
        snprintf(g_error, sizeof(g_error), "Could not open: %s", utilFileName(path));
        queueNext(&g_queue, true);
        mutexUnlock(&g_lock);
    }

    installDecoder(NULL);
    mutexLock(&g_lock);
    g_state = PLAYER_STOPPED;
    mutexUnlock(&g_lock);
}

static void stopPlayback(void)
{
    mutexLock(&g_lock);
    g_state = PLAYER_STOPPED;
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
static void writeSamples(s16 *dst, const float *src, u64 frames)
{
    for (u64 i = 0; i < frames * 2; i++) {
        float v = src[i];
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        dst[i] = (s16)(v * 32767.0f);
    }
}

// Decodes the next block into a buffer. False if there is nothing to play.
static bool fillBuffer(AudioOutBuffer *buffer)
{
    u64 done = 0;
    int empty_tracks = 0;
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
    writeSamples(buffer->buffer, g_mix, BUFFER_FRAMES);
    buffer->data_size = BUFFER_BYTES;
    buffer->data_offset = 0;
    return true;
}

static void applySeek(double seconds)
{
    if (!g_decoder)
        return;
    u64 frame = (u64)(seconds * decoderSampleRate(g_decoder));
    if (!decoderSeek(g_decoder, frame))
        return;
    resamplerReset(&g_resampler, decoderSampleRate(g_decoder), g_out_rate);
    mutexLock(&g_lock);
    g_played_frames = (u64)(seconds * g_out_rate);
    mutexUnlock(&g_lock);
}

static void handleCommands(void)
{
    mutexLock(&g_lock);
    bool load = g_load_pending;
    double seek = g_seek_target;
    g_load_pending = false;
    g_seek_target = -1.0;
    mutexUnlock(&g_lock);

    if (load)
        loadCurrent();
    else if (seek >= 0.0)
        applySeek(seek);
}

static void submitBuffers(void)
{
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (g_queued[i])
            continue;
        mutexLock(&g_lock);
        bool playing = g_state == PLAYER_PLAYING;
        mutexUnlock(&g_lock);
        if (!playing || !fillBuffer(&g_buffers[i]))
            return;
        if (R_SUCCEEDED(audoutAppendAudioOutBuffer(&g_buffers[i])))
            g_queued[i] = true;
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

void playerPlay(TrackList *list, size_t start)
{
    mutexLock(&g_lock);
    trackListFree(&g_list);
    queueFree(&g_queue);
    g_list = *list;
    *list = (TrackList){0};
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
    }
    mutexUnlock(&g_lock);
}

// Loads the queue's current track and plays it. Call with the lock held.
static void startCurrentLocked(void)
{
    if (g_list.count == 0)
        return;
    g_load_pending = true;
    g_seek_target = -1.0;
    g_played_frames = 0;
    g_duration = 0.0;
    g_state = PLAYER_PLAYING;
}

void playerTogglePause(void)
{
    mutexLock(&g_lock);
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
    out->track_count = g_list.count;
    out->position = g_out_rate ? (double)g_played_frames / g_out_rate : 0.0;
    out->duration = g_duration;
    out->shuffle = g_shuffle;
    out->repeat = g_repeat;
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
