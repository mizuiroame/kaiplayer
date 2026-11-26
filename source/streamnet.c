#include "streamnet.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "lang.h"
#include "net.h"

#define THREAD_STACK (256 * 1024)  // curl runs on it.
#define ANSWER_MAX (8u << 20)      // An item's metadata can be large (many files, reviews).
#define REQUEST_TIMEOUT_S 40L
#define REQUEST_STALL_S 15L
#define HTTP_TOO_MANY 429

// What went wrong, for the person.
static void netWhy(const NetResponse *r, char *why, size_t why_size)
{
    switch (r->problem) {
    case NET_NO_NAME:
    case NET_NO_CONNECT:
    case NET_TIMEOUT:
        snprintf(why, why_size, "%s", T("The Internet Archive didn't answer. Is the console connected to the internet?"));
        break;
    case NET_SECURE:
    case NET_CERTIFICATE: snprintf(why, why_size, "%s", T("The secure connection to the Internet Archive failed.")); break;
    case NET_STATUS:
        if (r->status == HTTP_TOO_MANY)
            snprintf(why, why_size, "%s", T("The Internet Archive is busy: try again in a minute."));
        else
            snprintf(why, why_size, T("The Internet Archive answered with an error (%ld)."), r->status);
        break;
    default: snprintf(why, why_size, "%s", T("The connection to the Internet Archive failed.")); break;
    }
}

// q's answer into list. False with why (empty if cancelled).
static bool ask(const StreamQuery *q, StreamList *list, const volatile bool *cancel, char *why, size_t why_size)
{
    char url[STREAM_URL_MAX];
    *list = (StreamList){.total = -1};
    if (!streamQueryUrl(q, url, sizeof(url))) {
        snprintf(why, why_size, "%s", T("That can't be searched for."));
        return false;
    }
    NetClient *client = netClientOpen();
    if (!client) {
        snprintf(why, why_size, "%s", T("The network can't be used."));
        return false;
    }
    NetRequest request = {.url = url, .cancel = cancel, .timeout_s = REQUEST_TIMEOUT_S,
                          .stall_s = REQUEST_STALL_S, .accept_json = true};
    NetResponse response;
    bool fetched = netFetchText(client, &request, ANSWER_MAX, &response);
    netClientClose(client);
    if (cancel && *cancel) {
        netResponseFree(&response);  // Nothing (or freed already) if it failed.
        why[0] = '\0';
        return false;
    }
    if (!fetched) {
        netWhy(&response, why, why_size);
        return false;
    }
    JsonDoc *doc = jsonParse(response.data, response.size);
    netResponseFree(&response);
    bool ok = doc && streamParseAnswer(q, jsonRoot(doc), list, why, why_size);
    if (!doc)
        snprintf(why, why_size, "%s", T("The Internet Archive's answer wasn't what was expected."));
    jsonFree(doc);
    return ok;
}

// --- The pages' thread ---

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static pthread_t g_thread;
static bool g_started, g_stop;
static unsigned g_serial;        // The last request's.
static bool g_waiting;           // A request not taken by the thread yet:
static StreamQuery g_query;      // this one.
static volatile bool g_cancel;   // The one on its way is called off.
static bool g_done;              // An answer for the main thread:
static unsigned g_done_serial;
static bool g_done_ok;
static StreamList g_done_list = {.total = -1};
static char g_done_why[STREAM_WHY_MAX];

static void *pagesThread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_lock);
    while (!g_stop) {
        if (!g_waiting) {
            pthread_cond_wait(&g_wake, &g_lock);
            continue;
        }
        StreamQuery q = g_query;
        unsigned serial = g_serial;
        g_waiting = false;
        g_cancel = false;
        pthread_mutex_unlock(&g_lock);
        StreamList list;
        char why[STREAM_WHY_MAX] = "";
        bool ok = ask(&q, &list, &g_cancel, why, sizeof(why));
        pthread_mutex_lock(&g_lock);
        if (g_cancel || serial != g_serial) {  // A newer one replaced it.
            streamListFree(&list);
            continue;
        }
        streamListFree(&g_done_list);
        g_done = true;
        g_done_serial = serial;
        g_done_ok = ok;
        g_done_list = list;
        snprintf(g_done_why, sizeof(g_done_why), "%s", why);
    }
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

bool streamNetStart(void)
{
    if (g_started)
        return true;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREAD_STACK);
    g_stop = false;
    g_started = pthread_create(&g_thread, &attr, pagesThread, NULL) == 0;
    pthread_attr_destroy(&attr);
    return g_started;
}

void streamNetStop(void)
{
    if (!g_started)
        return;
    pthread_mutex_lock(&g_lock);
    g_stop = true;
    g_cancel = true;
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
    pthread_join(g_thread, NULL);
    g_started = false;
    pthread_mutex_lock(&g_lock);
    streamListFree(&g_done_list);
    g_done = false;
    pthread_mutex_unlock(&g_lock);
}

unsigned streamFetch(const StreamQuery *q)
{
    if (!g_started)
        return 0;
    pthread_mutex_lock(&g_lock);
    if (++g_serial == 0)
        g_serial = 1;
    g_query = *q;
    g_waiting = true;
    g_cancel = true;  // The one on its way, if any: its answer isn't wanted now.
    unsigned serial = g_serial;
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
    return serial;
}

bool streamFetchTake(unsigned *serial, bool *ok, StreamList *list, char *why, size_t why_size)
{
    pthread_mutex_lock(&g_lock);
    bool done = g_done;
    if (done) {
        *serial = g_done_serial;
        *ok = g_done_ok;
        *list = g_done_list;
        g_done_list = (StreamList){.total = -1};
        snprintf(why, why_size, "%s", g_done_why);
        g_done = false;
    }
    pthread_mutex_unlock(&g_lock);
    return done;
}
