// The equalizer editor: Left/Right pick a band, Up/Down change its gain,
// X steps through the presets, Y turns the equalizer on or off.
#include <string.h>

#include "app.h"
#include "app_buttons.h"

static void save(App *app)
{
    appApplyAudioSettings(app);
    configSave(&app->config);
}

void appEqualizerSetGain(App *app, int band, int gain)
{
    if (band < 0 || band >= EQ_BANDS)
        return;
    Config *c = &app->config;
    gain = gain < EQ_GAIN_MIN ? EQ_GAIN_MIN : gain > EQ_GAIN_MAX ? EQ_GAIN_MAX : gain;
    app->eq_band = band;
    if (c->eq_on && c->eq_preset == EQ_PRESET_CUSTOM && c->eq_gains[band] == gain)
        return;  // Nothing changed (a drag sends this every frame).
    // Editing a preset turns it into Custom, starting from its gains.
    if (c->eq_preset != EQ_PRESET_CUSTOM) {
        int gains[EQ_BANDS];
        if (eqPresetGains((EqPreset)c->eq_preset, gains))
            memcpy(c->eq_gains, gains, sizeof(gains));
        c->eq_preset = EQ_PRESET_CUSTOM;
    }
    c->eq_gains[band] = gain;
    c->eq_on = true;
    save(app);
}

void appEqualizerNextPreset(App *app)
{
    app->config.eq_preset = (app->config.eq_preset + 1) % EQ_PRESET_COUNT;
    app->config.eq_on = true;
    save(app);
}

void appEqualizerToggle(App *app)
{
    app->config.eq_on = !app->config.eq_on;
    save(app);
}

// The gain shown for a band: the preset's, or the custom one.
static int bandGain(const App *app, int band)
{
    int gains[EQ_BANDS];
    if (eqPresetGains((EqPreset)app->config.eq_preset, gains))
        return gains[band];
    return app->config.eq_gains[band];
}

void appHandleEqualizer(App *app, uint64_t pressed)
{
    if (pressed & BUTTONS_LEFT)
        app->eq_band = app->eq_band > 0 ? app->eq_band - 1 : 0;
    if (pressed & BUTTONS_RIGHT)
        app->eq_band = app->eq_band + 1 < EQ_BANDS ? app->eq_band + 1 : EQ_BANDS - 1;
    if (pressed & BUTTONS_UP)
        appEqualizerSetGain(app, app->eq_band, bandGain(app, app->eq_band) + 1);
    if (pressed & BUTTONS_DOWN)
        appEqualizerSetGain(app, app->eq_band, bandGain(app, app->eq_band) - 1);
    if (pressed & HidNpadButton_X)
        appEqualizerNextPreset(app);
    if (pressed & HidNpadButton_Y)
        appEqualizerToggle(app);
    if (pressed & (HidNpadButton_B | HidNpadButton_Plus))
        app->eq_open = false;  // Back to Settings.
}
