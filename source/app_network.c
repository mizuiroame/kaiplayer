// The network folder (dav.h): its Settings page, its test, and the console's
// network, started only when there is a folder.
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "catalog_scan.h"
#include "davfile.h"
#include "davnet.h"
#include "kfile.h"
#include "keyboard_system.h"
#include "lang.h"
#include "net.h"
#include "smbnet.h"

#define TEST_TEXT_MAX 256
#define TEST_STACK (256 * 1024)  // libsmb2 and curl run on it.

static pthread_mutex_t g_test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_test_thread;
static bool g_test_started;  // The thread is to be joined (main thread only).
static bool g_testing, g_tested;
static char g_test_text[TEST_TEXT_MAX];

static void accountPath(char *out, size_t size)
{
    snprintf(out, size, "%s/" DAV_ACCOUNT_FILE, configFolder());
}

// The console's network, started for the network folder once there is one,
// and kept until KaiPlayer ends: nothing may open a socket before it, and a
// song still playing from the folder may use it after the folder is forgotten.
static bool g_network;

static void useNetwork(void)
{
    if (davConfigured() && !g_network) {
        g_network = netInit();
        if (g_network)
            kfileSetRemote(KFILE_REMOTE_FOLDER, davFileRemote());  // Network files can be read from now on.
    }
}

void appNetworkStart(App *app)
{
    char path[UTIL_PATH_MAX];
    accountPath(path, sizeof(path));
    davLoadAccount(path);
    useNetwork();
    if (app->config.source == LIBRARY_NETWORK && !davConfigured())
        app->config.source = LIBRARY_CARD;  // Forgotten: the card's.
}

bool appNetworkReady(void)
{
    return g_network && davConfigured();
}

void appNetworkShown(char *address, size_t address_size, char *user, size_t user_size)
{
    DavAccount a;
    davGetAccount(&a);
    davShownAddress(a.address, address, address_size);
    snprintf(user, user_size, "%s", a.user);
    memset(&a, 0, sizeof(a));
}

void appNetworkShownPath(const char *location, char *out, size_t size)
{
    char address[DAV_ADDRESS_MAX], user[DAV_TEXT_MAX];
    appNetworkShown(address, sizeof(address), user, sizeof(user));
    const char *rest = location + sizeof(DAV_PREFIX) - 2;  // From its '/'.
    snprintf(out, size, "%s%s", address, strcmp(rest, "/") ? rest : "");
}

static void save(App *app, const DavAccount *a)
{
    DavAccount before;
    davGetAccount(&before);
    bool moved = strcmp(before.address, a->address) != 0;
    memset(&before, 0, sizeof(before));
    davUseAccount(a);
    smbForget();  // The connections kept were the old account's.
    davFileForget();
    char path[UTIL_PATH_MAX];
    accountPath(path, sizeof(path));
    if (!davSaveAccount(path))
        appNotify(app, NOTICE_ERROR, T("It couldn't be written to the card."));
    useNetwork();
    appLibraryRemoteChanged(app, LIBRARY_NETWORK, moved);
}

static void typeAddress(App *app)
{
    DavAccount a;
    davGetAccount(&a);
    char shown[DAV_ADDRESS_MAX], text[DAV_ADDRESS_MAX], address[DAV_ADDRESS_MAX];
    davShownAddress(a.address, shown, sizeof(shown));
    // Room for "https://" and the last '/' that may be added.
    SystemKeyboardResult result = systemKeyboardText(T("Address: \\\\PC\\Music (a shared folder) or 192.168.0.10:8098 (WebDAV)"),
                                                     shown, text, sizeof(text), sizeof(text) - 10);
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."));
    } else if (result == SYSTEM_KEYBOARD_OK && !davNormalizeAddress(text, address, sizeof(address))) {
        appNotify(app, NOTICE_ERROR, T("That isn't a network folder's address: \\\\PC\\Music, smb://, http:// or https://."));
    } else if (result == SYSTEM_KEYBOARD_OK) {
        if (!davSameServer(address, a.address)) {  // Another server: the old one's password isn't sent there,
            a.user[0] = '\0';                     // and its certificate is checked again.
            memset(a.password, 0, sizeof(a.password));
            a.any_certificate = false;
        }
        memcpy(a.address, address, sizeof(a.address));
        save(app, &a);
        appNotify(app, NOTICE_DONE, T("Network folder set: test the connection to check it."));
    }
    memset(&a, 0, sizeof(a));
}

// After a password is typed: how the card keeps it.
static void sayHowKept(App *app, const DavAccount *a)
{
    if (!a->user[0] || !a->password[0])
        return;
    if (a->forget_password)
        appNotify(app, NOTICE_DONE, T("Saved until KaiPlayer closes: the password isn't kept on the card."));
    else if (davIsSmbAddress(a->address))
        appNotify(app, NOTICE_DONE, T("Saved: the card keeps the password's hash, which opens this share like the password. Use a user that can only read the music."));
    else
        appNotify(app, NOTICE_DONE, T("Saved: the password is only encoded on the card. Use a user that can only read the music."));
}

static void typeAccount(App *app)
{
    DavAccount a;
    davGetAccount(&a);
    if (!a.address[0]) {
        appToast(app, T("Enter the folder's address first."));
        memset(&a, 0, sizeof(a));
        return;
    }
    char user[DAV_TEXT_MAX], password[DAV_TEXT_MAX] = "";
    SystemKeyboardResult result = systemKeyboardText(T("User name (empty: none; DOMAIN\\user for a domain)"), a.user,
                                                     user, sizeof(user), sizeof(user) - 1);
    if (result == SYSTEM_KEYBOARD_OK && user[0])
        result = systemKeyboardPassword(T("Password"), password, sizeof(password), sizeof(password) - 1);
    if (result == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."));
    } else if (result == SYSTEM_KEYBOARD_OK) {
        snprintf(a.user, sizeof(a.user), "%s", user);
        if (davSetPassword(&a, password)) {
            save(app, &a);
            sayHowKept(app, &a);
        } else {
            appNotify(app, NOTICE_ERROR, T("That password can't be used."));
        }
    }
    memset(password, 0, sizeof(password));
    memset(&a, 0, sizeof(a));
}

bool appNetworkEnsurePassword(App *app)
{
    if (!davPasswordMissing())
        return true;
    DavAccount a;
    davGetAccount(&a);
    char password[DAV_TEXT_MAX] = "", prompt[DAV_ADDRESS_MAX + 64], shown[DAV_ADDRESS_MAX];
    davShownAddress(a.address, shown, sizeof(shown));
    snprintf(prompt, sizeof(prompt), T("Password for %s (%s)"), a.user, shown);
    SystemKeyboardResult result = systemKeyboardPassword(prompt, password, sizeof(password), sizeof(password) - 1);
    bool ok = result == SYSTEM_KEYBOARD_OK && password[0] && davSetPassword(&a, password);
    if (ok) {
        davUseAccount(&a);  // Kept until KaiPlayer closes; the card's file stays without it.
        smbForget();
        davFileForget();
        // Its library, read until now without the password, is read again.
        if (app->config.source == LIBRARY_NETWORK)
            catalogScanStart(DAV_ROOT);
        else
            catalogScanRefreshRemote(DAV_ROOT, false);
    } else if (result == SYSTEM_KEYBOARD_UNAVAILABLE) {
        appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."));
    } else {
        appNotify(app, NOTICE_ERROR, T("Without the password the network folder can't be read."));
    }
    memset(password, 0, sizeof(password));
    memset(&a, 0, sizeof(a));
    return ok;
}

static void toggleRemember(App *app)
{
    DavAccount a;
    davGetAccount(&a);
    a.forget_password = !a.forget_password;
    save(app, &a);  // Not remembered: the password leaves the card's file now.
    appToast(app, a.forget_password ? T("The password won't be kept on the card: KaiPlayer asks for it when the folder is used.")
                                    : T("The password is kept on the card."));
    memset(&a, 0, sizeof(a));
}

static void toggleUnsigned(App *app)
{
    DavAccount a;
    davGetAccount(&a);
    if (!davIsSmbAddress(a.address)) {
        appToast(app, T("Signing is only for shared folders (SMB)."));
    } else {
        a.allow_unsigned = !a.allow_unsigned;
        save(app, &a);
        appToast(app, a.allow_unsigned ? T("Not signed: someone on your network could change what's read. Use it "
                                           "only for an old NAS.")
                                       : T("The connection is signed again."));
    }
    memset(&a, 0, sizeof(a));
}

// Only an https folder's connections use it.
static void toggleCertificate(App *app)
{
    DavAccount a;
    davGetAccount(&a);
    a.any_certificate = !a.any_certificate;
    save(app, &a);
    memset(&a, 0, sizeof(a));
}

void appNetworkChoices(bool *remember, bool *allow_unsigned, bool *smb, bool *check_certificate)
{
    DavAccount a;
    davGetAccount(&a);
    *remember = !a.forget_password;
    *allow_unsigned = a.allow_unsigned;
    *smb = davIsSmbAddress(a.address);
    *check_certificate = !a.any_certificate;
    memset(&a, 0, sizeof(a));
}

static void *testThread(void *arg)
{
    (void)arg;
    char text[TEST_TEXT_MAX];
    davTest(text, sizeof(text));
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

static void test(App *app)
{
    if (!davConfigured()) {
        appToast(app, T("Enter the folder's address first."));
        return;
    }
    if (!g_network) {
        appNotify(app, NOTICE_ERROR, T("The network can't be used."));
        return;
    }
    if (!appNetworkEnsurePassword(app))
        return;
    joinTest(false);
    if (g_test_started) {  // One is running.
        appToast(app, T("Testing the connection..."));
        return;
    }
    pthread_mutex_lock(&g_test_lock);
    g_testing = true;
    g_tested = false;
    pthread_mutex_unlock(&g_test_lock);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, TEST_STACK);
    g_test_started = pthread_create(&g_test_thread, &attr, testThread, NULL) == 0;
    pthread_attr_destroy(&attr);
    if (!g_test_started) {
        pthread_mutex_lock(&g_test_lock);
        g_testing = false;
        pthread_mutex_unlock(&g_test_lock);
    }
    appToast(app, g_test_started ? T("Testing the connection...") : T("The network can't be used."));
}

static void forget(App *app)
{
    DavAccount none = {0};
    save(app, &none);
    appNotify(app, NOTICE_DONE, T("The network folder is forgotten."));
}

bool appNetworkActivate(App *app, SettingRow row)
{
    switch (row) {
    case SETTING_NET_ADDRESS: typeAddress(app); return true;
    case SETTING_NET_ACCOUNT: typeAccount(app); return true;
    case SETTING_NET_TEST: test(app); return true;
    case SETTING_NET_FORGET: forget(app); return true;
    case SETTING_NET_REMEMBER: toggleRemember(app); return true;
    case SETTING_NET_UNSIGNED: toggleUnsigned(app); return true;
    case SETTING_NET_CERTIFICATE: toggleCertificate(app); return true;
    default: return false;
    }
}

void appNetworkUpdate(App *app)
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
}

void appNetworkExit(void)
{
    joinTest(true);
    kfileSetRemote(KFILE_REMOTE_FOLDER, NULL);
    smbForget();
    davFileForget();
    if (g_network)
        netExit();
    g_network = false;
}
