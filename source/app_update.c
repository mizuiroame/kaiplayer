// Settings > About: the daily look for a new version, and what it found
// said on screen (updatefetch.h does the asking and installing).
#include <stdio.h>
#include <time.h>

#include "app.h"
#include "lang.h"
#include "settings.h"
#include "update.h"
#include "updatefetch.h"

static void saveConfig(App *app)
{
    if (!configSave(&app->config))
        appNotify(app, NOTICE_ERROR, T("It couldn't be written to the card."));
}

void appStartUpdates(App *app, const char *nro_path)
{
    updateFetchInit(nro_path);
    if (app->config.update_check && updateDue(app->config.update_checked, (int64_t)time(NULL)))
        updateFetchCheck();  // Quietly: only a new version is said.
}

void appStopUpdates(void)
{
    updateFetchExit();
}

// "KaiPlayer 1.3.0 is out: ...", by where KaiPlayer runs from.
static void sayFound(App *app, const UpdateStatus *s)
{
    const char *version = s->tag[0] == 'v' ? s->tag + 1 : s->tag;
    char line[NOTICE_TEXT_MAX];
    if (s->place == UPDATE_PLACE_BREWSTORE)
        snprintf(line, sizeof(line), T("KaiPlayer %s is out: update it in Brewstore."), version);
    else if (s->place == UPDATE_PLACE_LOOSE)
        snprintf(line, sizeof(line), T("KaiPlayer %s is out: Settings > About installs it."), version);
    else
        snprintf(line, sizeof(line), T("KaiPlayer %s is out, on its GitHub page."), version);
    appToast(app, line);
}

void appUpdatesTick(App *app)
{
    UpdateStatus s;
    updateFetchStatus(&s);
    if (s.serial == app->update_seen)
        return;
    app->update_seen = s.serial;
    bool manual = app->update_manual;
    switch (s.state) {
    case UPDATE_CURRENT:
        app->config.update_checked = (int64_t)time(NULL);
        app->config.update_tag[0] = '\0';
        saveConfig(app);
        if (manual)
            appNotify(app, NOTICE_DONE, T("KaiPlayer is up to date."));
        break;
    case UPDATE_FOUND:
        app->config.update_checked = (int64_t)time(NULL);
        snprintf(app->config.update_tag, sizeof(app->config.update_tag), "%s", s.tag);
        saveConfig(app);
        sayFound(app, &s);
        break;
    case UPDATE_CHECK_FAILED:
        app->config.update_checked = (int64_t)time(NULL);  // Not asked again on every start.
        saveConfig(app);
        if (manual)
            appNotify(app, NOTICE_ERROR, s.error);
        break;
    case UPDATE_INSTALLED: {
        char line[NOTICE_TEXT_MAX];
        snprintf(line, sizeof(line), T("KaiPlayer %s is installed: it runs from the next start."),
                 s.tag[0] == 'v' ? s.tag + 1 : s.tag);
        app->config.update_tag[0] = '\0';
        saveConfig(app);
        appNotify(app, NOTICE_DONE, line);
        break;
    }
    case UPDATE_INSTALL_FAILED:
        if (s.error[0])
            appNotify(app, NOTICE_ERROR, s.error);
        break;
    default: break;
    }
}

void appUpdatesActivate(App *app)
{
    UpdateStatus s;
    updateFetchStatus(&s);
    app->update_manual = true;
    if (s.state == UPDATE_CHECKING || s.state == UPDATE_DOWNLOADING) {
        appToast(app, T("Already on it."));
    } else if (s.state == UPDATE_FOUND && s.place == UPDATE_PLACE_BREWSTORE) {
        appToast(app, T("Update it in Brewstore."));
    } else if (s.state == UPDATE_FOUND && s.place == UPDATE_PLACE_LOOSE) {
        updateFetchInstall();
    } else if (s.state == UPDATE_INSTALLED) {
        appToast(app, T("Close KaiPlayer and open it again to use the new version."));
    } else {
        updateFetchCheck();
    }
}

void appUpdatesToggle(App *app)
{
    app->config.update_check = !app->config.update_check;
    saveConfig(app);
}

void appUpdatesValue(const App *app, int row, char *out, size_t size)
{
    if (row == SETTING_UPDATE_AUTO) {
        snprintf(out, size, "%s", app->config.update_check ? T("On") : T("Off"));
        return;
    }
    UpdateStatus s;
    updateFetchStatus(&s);
    const char *tag = s.tag[0] ? s.tag : app->config.update_tag;
    const char *version = tag[0] == 'v' ? tag + 1 : tag;
    switch (s.state) {
    case UPDATE_CHECKING: snprintf(out, size, "%s", T("Looking...")); break;
    case UPDATE_CURRENT: snprintf(out, size, "%s", T("Up to date")); break;
    case UPDATE_FOUND:
        if (s.place == UPDATE_PLACE_LOOSE)
            snprintf(out, size, T("%s is out: A installs it"), version);
        else if (s.place == UPDATE_PLACE_BREWSTORE)
            snprintf(out, size, T("%s is out, in Brewstore"), version);
        else
            snprintf(out, size, T("%s is out"), version);
        break;
    case UPDATE_DOWNLOADING:
        snprintf(out, size, T("Downloading %s... %d%%"), version, (int)(s.progress * 100.0f));
        break;
    case UPDATE_INSTALLED: snprintf(out, size, T("%s installed: open KaiPlayer again"), version); break;
    case UPDATE_CHECK_FAILED: snprintf(out, size, "%s", T("Couldn't look: A tries again")); break;
    case UPDATE_INSTALL_FAILED: snprintf(out, size, "%s", T("Not installed: A tries again")); break;
    default:  // Nothing asked yet since KaiPlayer started: what was found before, if it's still newer.
        if (updateIsNewer(app->config.update_tag, KAIPLAYER_VERSION))
            snprintf(out, size, T("%s is out: A looks again"), version);
        else
            snprintf(out, size, T("This is %s: A looks now"), KAIPLAYER_VERSION);
        break;
    }
}
