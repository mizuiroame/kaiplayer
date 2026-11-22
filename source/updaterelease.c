#include "updaterelease.h"

#include <stdio.h>
#include <string.h>

#include "json.h"

static const char *text(const JsonValue *object, const char *name)
{
    const JsonValue *item = jsonGet(object, name);
    return item && item->type == JSON_STRING ? item->text : NULL;
}

static bool isTrue(const JsonValue *object, const char *name)
{
    const JsonValue *item = jsonGet(object, name);
    return item && item->type == JSON_TRUE;
}

bool updateReadRelease(const char *json, const char *repo, const char *nro_name, UpdateRelease *out)
{
    memset(out, 0, sizeof(*out));
    JsonDoc *doc = json ? jsonParse(json, strlen(json)) : NULL;
    const JsonValue *root = doc ? jsonRoot(doc) : NULL;
    const char *tag = text(root, "tag_name");
    bool ok = root && root->type == JSON_OBJECT && tag && strlen(tag) < sizeof(out->tag) &&
              updateIsNewer(tag, "0.0.0") && !isTrue(root, "prerelease") && !isTrue(root, "draft");
    const JsonValue *asset = NULL;
    if (ok) {
        const JsonValue *assets = jsonGet(root, "assets");
        for (const JsonValue *each = assets && assets->type == JSON_ARRAY ? assets->child : NULL; each;
             each = each->next) {
            const char *name = text(each, "name");
            if (each->type == JSON_OBJECT && name && strcmp(name, nro_name) == 0) {
                asset = each;
                break;
            }
        }
        ok = asset != NULL;
    }
    if (ok) {
        const char *url = text(asset, "browser_download_url");
        const JsonValue *size = jsonGet(asset, "size");
        ok = updateUrlOk(url, repo, tag, nro_name) && updateDigest(text(asset, "digest"), out->sha256) && size &&
             size->type == JSON_NUMBER && size->number > 0 && size->number <= UPDATE_NRO_MAX;
        if (ok) {
            snprintf(out->tag, sizeof(out->tag), "%s", tag);
            snprintf(out->url, sizeof(out->url), "%s", url);
            out->size = (int64_t)size->number;
        }
    }
    jsonFree(doc);
    if (!ok)
        memset(out, 0, sizeof(*out));
    return ok;
}
