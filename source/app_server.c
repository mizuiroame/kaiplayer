// The Navidrome server (subsonic.h): its Settings page, its test, the songs
// played told to it (scrobbling), and the console's network, started only
// when there is a server.
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "kfile.h"
#include "keyboard_system.h"
#include "lang.h"
#include "net.h"
#include "subfile.h"
#include "subnet.h"

#define TEST_TEXT_MAX 256
#define THREAD_STACK (256 * 1024)  // curl runs on it.
#define SALT_BYTES 8               // 16 hex digits.
#define SCROBBLE_QUEUE 8
#define PLAYED_SECONDS 240.0       // A song counts as played after half of it, or this long.
#define RESTART_SECONDS 3.0

static void accountPath(char *out, size_t size)
{
    snprintf(out, size, "%s/" SUB_ACCOUNT_FILE, configFolder());
}

// The console's network, started for the server once there is one, and kept
// until KaiPlayer ends (net.c counts its users: the network folder is another).
static bool g_network;

static void useNetwork(void)
{
    if (subConfigured() && !g_network) {
        g_network = netInit();
        if (g_network)
            kfileSetRemote(KFILE_REMOTE_SERVER, subFileRemote());  // The server's songs can be read from now on.
    }
}

void appServerStart(App *app)
{
    char path[UTIL_PATH_MAX];
    accountPath(path, sizeof(path));
    subLoadAccount(path);
    useNetwork();
    if (app->config.source == LIBRARY_SERVER && !subSignedInNow())
        app->config.source = LIBRARY_CARD;  // Forgotten, or signed out: the card's.
}

bool appServerReady(void)
{
    return g_network && subSignedInNow();
}

void appServerShown(char *address, size_t address_size, char *user, size_t user_size, bool *check_certificate)
{
    SubAccount a;
    subGetAccount(&a);
    subShownAddress(a.address, address, address_size);
    snprintf(user, user_size, "%s", subSignedIn(&a) ? a.user : "");
    *check_certificate = a.check_certificate;
    memset(&a, 0, sizeof(a));
}

// Keeps the account. library: the library may have changed (another server or user), so it's read again.
static void save(App *app, const SubAccount *a, bool library)
{
    SubAccount before;
    subGetAccount(&before);
    bool moved = strcmp(before.address, a->address) != 0;
    memset(&before, 0, sizeof(before));
    subUseAccount(a);
    subFileForget();  // The connection kept was the old account's.
    char path[UTIL_PATH_MAX];
    accountPath(path, sizeof(path));
    if (!subSaveAccount(path))
        appToast(app, T("It couldn't be written to the card."));
    useNetwork();
    if (library)
        appLibraryRemoteChanged(app, LIBRARY_SERVER, moved);
}

// --- The test ---

static pthread_mutex_t g_test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_test_thread;
static bool g_test_started;  // The thread is to be joined (main thread only).
static bool g_testing, g_tested;
static char g_test_text[TEST_TEXT_MAX];

static void *testThread(void *arg)
{
    (void)arg;
    char text[TEST_TEXT_MAX];
    subTest(text, sizeof(text));
    pthread_mutex_lock(&g_test_lock);
    snprintf(g_test_text, sizeof(g_test_text), "%s", text);
    g_testing = false;
    g_tested = true;
    pthread_mutex_unlock(&g_test_lock);
    return NULL;
}

// The last test's thread, once it has ended (or at the end, waiting for it).
static void joinTest(bool wait)
{
    pthread_mutex_lock(&g_test_lock);
    bool done = !g_testing;
    pthread_mutex_unlock(&g_test_lock);
    if (g_test_started && (done || wait)) {
        pthread_join(g_test_thread, NULL);
        g_test_started = false;
    }
}

static bool startThread(pthread_t *thread, void *(*run)(void *))
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREAD_STACK);
    bool started = pthread_create(thread, &attr, run, NULL) == 0;
    pthread_attr_destroy(&attr);
    return started;
}

static void test(App *app)
{
    if (!subConfigured()) {
        appToast(app, T("Enter the server's address first."));
        return;
    }
    if (!g_network) {
        appToast(app, T("The network can't be used."));
        return;
    }
    joinTest(false);
    if (g_test_started) {  // One is running.
        appToast(app, T("Testing the connection..."));
        return;
    }
    pthread_mutex_lock(&g_test_lock);
    g_testing = true;
    g_tested = false;
    pthread_mutex_unlock(&g_test_lock);
    g_test_started = startThread(&g_test_thread, testThread);
    if (!g_test_started) {
        pthread_mutex_lock(&g_test_lock);
        g_testing = false;
        pthread_mutex_unlock(&g_test_lock);
    }
    appToast(app, g_test_started ? T("Testing the connection...") : T("The network can't be used."));
}

// --- The rows ---

static void typeAddress(App *app)
{
    SubAccount a;
    subGetAccount(&a);
    char shown[SUB_ADDRESS_MAX], text[SUB_ADDRESS_MAX], address[SUB_ADDRESS_MAX];
    subShownAddress(a.address, shown, sizeof(shown));
    char prompt[128];
    snprintf(prompt, sizeof(prompt), T("%s server, like 192.168.0.10:4533"), "Navidrome");
    // Room for the "http://" that may be added.
    SystemKeyboardResult result = systemKeyboardText(prompt, shown, text, sizeof(text), sizeof(text) - 10);
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appToast(app, T("The keyboard couldn't open."));
    } else if (result == SYSTEM_KEYBOARD_OK && !subNormalizeAddress(text, address, sizeof(address))) {
        appToast(app, T("That isn't a server's address."));
    } else if (result == SYSTEM_KEYBOARD_OK) {
        if (!subSameServer(address, a.address)) {  // Another server: the old one's sign-in isn't sent there.
            a.user[0] = a.salt[0] = '\0';
            memset(a.token, 0, sizeof(a.token));
        }
        memcpy(a.address, address, sizeof(a.address));
        save(app, &a, true);
        appToast(app, subSignedIn(&a) ? T("Server set: test the connection to check it.")
                                      : T("Server set: now enter your user name and password."));
    }
    memset(&a, 0, sizeof(a));
}

// Some random letters for the sign-in's token, new each time a password is typed.
static void makeSalt(char *out, size_t size)
{
    uint8_t bytes[SALT_BYTES];
    randomGet(bytes, sizeof(bytes));
    for (size_t i = 0; i < sizeof(bytes) && 2 * i + 2 < size; i++)
        snprintf(out + 2 * i, 3, "%02x", bytes[i]);
}

static void typeAccount(App *app)
{
    SubAccount a;
    subGetAccount(&a);
    if (!a.address[0]) {
        appToast(app, T("Enter the server's address first."));
        memset(&a, 0, sizeof(a));
        return;
    }
    char user[SUB_TEXT_MAX], password[SUB_TEXT_MAX] = "", prompt[128];
    snprintf(prompt, sizeof(prompt), T("%s user name"), "Navidrome");
    SystemKeyboardResult result = systemKeyboardText(prompt, a.user, user, sizeof(user), sizeof(user) - 1);
    if (result == SYSTEM_KEYBOARD_OK && user[0])
        result = systemKeyboardPassword(T("Password"), password, sizeof(password), sizeof(password) - 1);
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appToast(app, T("The keyboard couldn't open."));
    } else if (result == SYSTEM_KEYBOARD_OK) {
        char salt[SUB_SALT_MAX] = "";
        makeSalt(salt, sizeof(salt));
        snprintf(a.user, sizeof(a.user), "%s", user);
        a.salt[0] = '\0';
        memset(a.token, 0, sizeof(a.token));
        if (user[0] && !subSetPassword(&a, password, salt))
            a.user[0] = '\0';  // Too long to use.
        save(app, &a, true);
        if (subSignedIn(&a))
            test(app);  // Says at once whether the password was right.
    }
    memset(password, 0, sizeof(password));
    memset(&a, 0, sizeof(a));
}

static void toggleCertificate(App *app)
{
    SubAccount a;
    subGetAccount(&a);
    a.check_certificate = !a.check_certificate;
    save(app, &a, false);
    memset(&a, 0, sizeof(a));
}

static void forget(App *app)
{
    SubAccount none = {.check_certificate = true};
    save(app, &none, true);
    appToast(app, T("The Navidrome server is forgotten."));
}

bool appServerActivate(App *app, SettingRow row)
{
    switch (row) {
    case SETTING_SRV_ADDRESS: typeAddress(app); return true;
    case SETTING_SRV_ACCOUNT: typeAccount(app); return true;
    case SETTING_SRV_TEST: test(app); return true;
    case SETTING_SRV_CERTIFICATE: toggleCertificate(app); return true;
    case SETTING_SRV_FORGET: forget(app); return true;
    default: return false;
    }
}

// --- Telling the server what's played ---

typedef struct {
    char location[UTIL_PATH_MAX];
    bool submission;  // Played; else "now playing".
} Scrobble;

static pthread_mutex_t g_scrobble_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_scrobble_wake = PTHREAD_COND_INITIALIZER;
static pthread_t g_scrobble_thread;
static bool g_scrobble_started, g_scrobble_stop;
static Scrobble g_scrobbles[SCROBBLE_QUEUE];
static size_t g_scrobble_first, g_scrobble_count;

static void *scrobbleThread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_scrobble_lock);
    while (!g_scrobble_stop) {
        if (!g_scrobble_count) {
            pthread_cond_wait(&g_scrobble_wake, &g_scrobble_lock);
            continue;
        }
        Scrobble s = g_scrobbles[g_scrobble_first];
        g_scrobble_first = (g_scrobble_first + 1) % SCROBBLE_QUEUE;
        g_scrobble_count--;
        pthread_mutex_unlock(&g_scrobble_lock);
        subScrobble(s.location, s.submission);  // Not said if it fails: the music matters more.
        pthread_mutex_lock(&g_scrobble_lock);
    }
    pthread_mutex_unlock(&g_scrobble_lock);
    return NULL;
}

// Leaves it for the thread (started the first time); the oldest goes if too many wait.
static void queueScrobble(const char *location, bool submission)
{
    if (!g_scrobble_started && !(g_scrobble_started = startThread(&g_scrobble_thread, scrobbleThread)))
        return;
    pthread_mutex_lock(&g_scrobble_lock);
    if (g_scrobble_count == SCROBBLE_QUEUE) {
        g_scrobble_first = (g_scrobble_first + 1) % SCROBBLE_QUEUE;
        g_scrobble_count--;
    }
    Scrobble *s = &g_scrobbles[(g_scrobble_first + g_scrobble_count++) % SCROBBLE_QUEUE];
    snprintf(s->location, sizeof(s->location), "%s", location);
    s->submission = submission;
    pthread_cond_signal(&g_scrobble_wake);
    pthread_mutex_unlock(&g_scrobble_lock);
}

// The song playing, and what the server was told of it.
static struct {
    unsigned generation;
    size_t track;
    bool known, server, now_sent, played_sent;
    char location[UTIL_PATH_MAX];
} g_playing;

static void trackPlays(const PlayerStatus *status)
{
    if (status->state == PLAYER_STOPPED || status->track_count == 0)
        return;
    if (!g_playing.known || status->generation != g_playing.generation || status->track != g_playing.track) {
        g_playing.known = true;
        g_playing.generation = status->generation;
        g_playing.track = status->track;
        g_playing.now_sent = g_playing.played_sent = false;
        g_playing.server = playerTrackPath(status->track, g_playing.location, sizeof(g_playing.location)) &&
                           subIsLocation(g_playing.location);
    }
    if (!g_playing.server || !appServerReady())
        return;
    double needed = status->duration > 0 && status->duration / 2 < PLAYED_SECONDS ? status->duration / 2
                                                                                 : PLAYED_SECONDS;
    if (g_playing.played_sent && status->position < RESTART_SECONDS && status->position < needed)
        g_playing.now_sent = g_playing.played_sent = false;  // The same song from its start again.
    if (!g_playing.now_sent && status->state == PLAYER_PLAYING) {
        g_playing.now_sent = true;
        queueScrobble(g_playing.location, false);
    }
    if (!g_playing.played_sent && status->position >= needed) {
        g_playing.played_sent = true;
        queueScrobble(g_playing.location, true);
    }
}

void appServerUpdate(App *app, const PlayerStatus *status)
{
    char text[TEST_TEXT_MAX] = "";
    pthread_mutex_lock(&g_test_lock);
    if (g_tested) {
        snprintf(text, sizeof(text), "%s", g_test_text);
        g_tested = false;
    }
    pthread_mutex_unlock(&g_test_lock);
    if (text[0]) {
        joinTest(false);
        appToast(app, text);
    }
    trackPlays(status);
}

void appServerExit(void)
{
    joinTest(true);
    if (g_scrobble_started) {  // What's left unsaid is dropped; the one being sent ends first.
        pthread_mutex_lock(&g_scrobble_lock);
        g_scrobble_stop = true;
        pthread_cond_signal(&g_scrobble_wake);
        pthread_mutex_unlock(&g_scrobble_lock);
        pthread_join(g_scrobble_thread, NULL);
        g_scrobble_started = false;
    }
    kfileSetRemote(KFILE_REMOTE_SERVER, NULL);
    subFileForget();
    if (g_network)
        netExit();
    g_network = false;
}
