// Equalizer response (measured with sine tones) and ReplayGain tags.
#include <math.h>
#include <string.h>

#include "check.h"
#include "eq.h"
#include "tags.h"
#include "replaygain.h"
#include "theme.h"

#define RATE 48000.0
#define FRAMES 48000
#define PI 3.14159265358979323846

// Level in dB of a sine through the equalizer, after the filters settle.
static double responseDb(Eq *eq, double frequency)
{
    static float buf[FRAMES * 2];
    for (int i = 0; i < FRAMES; i++)
        buf[i * 2] = buf[i * 2 + 1] = (float)(0.25 * sin(2.0 * PI * frequency * i / RATE));
    eqReset(eq);
    eqProcess(eq, buf, FRAMES);
    double sum = 0.0;
    for (int i = FRAMES / 2; i < FRAMES; i++)
        sum += (double)buf[i * 2] * buf[i * 2];
    double rms = sqrt(sum / (FRAMES / 2));
    return 20.0 * log10(rms / (0.25 / sqrt(2.0)));
}

static void testFlat(void)
{
    Eq eq = {0};
    int gains[EQ_BANDS] = {0};
    eqConfigure(&eq, gains, RATE);
    CHECK(!eq.active);
    float buf[4] = {0.5f, -0.5f, 0.1f, 0.2f};
    eqProcess(&eq, buf, 2);
    CHECK(buf[0] == 0.5f && buf[3] == 0.2f);  // Untouched.
}

static void testBoostAndCut(void)
{
    Eq eq = {0};
    int gains[EQ_BANDS] = {0};
    gains[5] = 6;  // 1 kHz up 6 dB; the preamp takes 6 dB off everything.
    eqConfigure(&eq, gains, RATE);
    CHECK(eq.active && fabsf(eq.preamp - 0.501f) < 0.01f);
    CHECK(fabs(responseDb(&eq, 1000.0)) < 0.5);         // +6 - 6.
    CHECK(fabs(responseDb(&eq, 8000.0) + 6.0) < 1.0);   // Only the preamp.

    memset(gains, 0, sizeof(gains));
    gains[2] = -12;  // 125 Hz down 12 dB; nothing is boosted, so no preamp.
    eqConfigure(&eq, gains, RATE);
    CHECK(eq.preamp == 1.0f);
    CHECK(fabs(responseDb(&eq, 125.0) + 12.0) < 1.5);
    CHECK(fabs(responseDb(&eq, 4000.0)) < 0.5);

    gains[2] = 99;  // Clamped to +12.
    eqConfigure(&eq, gains, RATE);
    CHECK(fabsf(eq.preamp - 0.251f) < 0.01f);

    // Neighbouring boosts add up; the preamp covers the sum, so no tone
    // comes out louder than it went in.
    for (int b = 0; b < EQ_BANDS; b++)
        gains[b] = b < 4 ? 12 : 0;
    eqConfigure(&eq, gains, RATE);
    CHECK(eq.preamp < 0.2f);
    CHECK(responseDb(&eq, 45.0) < 0.3 && responseDb(&eq, 90.0) < 0.3 && responseDb(&eq, 180.0) < 0.3);

    // At 22.05 kHz the 16 kHz band is above Nyquist and stays flat.
    memset(gains, 0, sizeof(gains));
    gains[9] = 12;
    eqConfigure(&eq, gains, 22050.0);
    CHECK(!eq.active);
}

static void testPresets(void)
{
    int gains[EQ_BANDS];
    CHECK(eqPresetGains(EQ_PRESET_BASS, gains) && gains[0] > 0 && gains[9] == 0);
    CHECK(!eqPresetGains(EQ_PRESET_CUSTOM, gains));
    CHECK(strcmp(eqPresetName(EQ_PRESET_ROCK), "Rock") == 0 && eqPresetName(EQ_PRESET_COUNT)[0] == '\0');
    CHECK(strcmp(eqBandLabel(5), "1k") == 0 && eqBandFrequency(9) == 16000.0 && eqBandFrequency(10) == 0.0);
}

static void testReplayGain(void)
{
    TrackTags t;
    tagsInit(&t);
    tagsApplyComment("REPLAYGAIN_TRACK_GAIN=-6.54 dB", 30, &t);
    tagsApplyComment("replaygain_album_gain=+2.10 dB", 30, &t);
    CHECK(t.has_track_gain && fabsf(t.track_gain_db + 6.54f) < 0.001f);
    CHECK(t.has_album_gain && fabsf(t.album_gain_db - 2.10f) < 0.001f);
    tagsFree(&t);

    tagsApplyReplayGain("REPLAYGAIN_TRACK_GAIN", "loud", &t);   // Not a number.
    tagsApplyReplayGain("REPLAYGAIN_TRACK_GAIN", "99 dB", &t);  // Out of range.
    tagsApplyReplayGain("REPLAYGAIN_TRACK_PEAK", "0.9", &t);    // Not a gain.
    CHECK(!t.has_track_gain && !t.has_album_gain);

    // ID3 TXXX frame inside a v2.3 tag, and an APIC skipped on request.
    static const uint8_t tag[] = {
        'I', 'D', '3', 3, 0, 0, 0, 0, 0, 59,
        'T', 'X', 'X', 'X', 0, 0, 0, 31, 0, 0,
        0, 'R', 'E', 'P', 'L', 'A', 'Y', 'G', 'A', 'I', 'N', '_', 'T', 'R', 'A', 'C', 'K', '_', 'G', 'A', 'I', 'N', 0,
        '-', '3', '.', '5', ' ', 'd', 'B',
        'A', 'P', 'I', 'C', 0, 0, 0, 8, 0, 0, 0, 'i', '/', 'p', 0, 3, 0, 'X',
    };
    tagsInit(&t);
    t.skip_picture = true;
    tagsParseId3v2(tag, sizeof(tag), &t);
    CHECK(t.has_track_gain && fabsf(t.track_gain_db + 3.5f) < 0.001f);
    CHECK(t.picture == NULL);
    tagsFree(&t);
}

static void testTheme(void)
{
    CHECK(strcmp(themeAccentName(0), "Pink") == 0 && themeAccentName(THEME_ACCENT_COUNT)[0] == '\0');
    uint32_t pink = themeAccentColor(0, 0, false);
    CHECK(themeAccentColor(THEME_ACCENT_FROM_COVER, 0xFF808080u, false) == pink);  // No cover.
    uint32_t silver = themeAccentColor(THEME_ACCENT_FROM_COVER, 0, true);           // Black and white cover.
    CHECK(silver != pink && (silver & 0xFF) == ((silver >> 16) & 0xFF) - 10);
    CHECK(themeVivid(0xFF808080u) == silver);  // Grey: no hue to take.
    // A dark, dull blue cover becomes a bright, saturated blue.
    uint32_t vivid = themeAccentColor(THEME_ACCENT_FROM_COVER, 0xFF402010u, true);  // r=0x10 g=0x20 b=0x40
    unsigned r = vivid & 0xFF, g = (vivid >> 8) & 0xFF, b = (vivid >> 16) & 0xFF;
    CHECK(b >= 220 && b > g && g > r);
    CHECK(themeAccentColor(99, 0, false) == pink);  // Unknown accent.
}

static void testReplayGainChoice(void)
{
    TrackTags t;
    tagsInit(&t);
    t.has_track_gain = t.has_album_gain = true;
    t.track_gain_db = -7.0f;
    t.album_gain_db = -5.0f;
    CHECK(replayGainDb(REPLAYGAIN_OFF, false, false, &t) == 0.0f);
    CHECK(replayGainDb(REPLAYGAIN_TRACK, false, false, &t) == -7.0f);
    CHECK(replayGainDb(REPLAYGAIN_ALBUM, true, true, &t) == -5.0f);
    CHECK(replayGainDb(REPLAYGAIN_AUTO, false, false, &t) == -5.0f);  // An album in order.
    CHECK(replayGainDb(REPLAYGAIN_AUTO, true, false, &t) == -7.0f);   // Shuffle.
    CHECK(replayGainDb(REPLAYGAIN_AUTO, false, true, &t) == -7.0f);   // A playlist.
    t.has_album_gain = false;
    CHECK(replayGainDb(REPLAYGAIN_ALBUM, false, false, &t) == -7.0f); // Falls back to track.
    t.has_track_gain = false;
    CHECK(replayGainDb(REPLAYGAIN_AUTO, false, false, &t) == 0.0f);   // No tags.
    CHECK(replayGainFactor(6.0f) == 1.0f && fabsf(replayGainFactor(-6.0f) - 0.501f) < 0.01f);
    tagsFree(&t);
}

void testEq(void)
{
    testReplayGainChoice();
    testTheme();
    testFlat();
    testBoostAndCut();
    testPresets();
    testReplayGain();
}
