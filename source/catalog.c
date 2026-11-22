#include "catalog.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

#define CACHE_HEADER "KAIPLAYER-CATALOG 3"
#define CACHE_HEADER_V2 "KAIPLAYER-CATALOG 2"  // Before Opus played: its .ogg songs were kept without tags.
#define CACHE_HEADER_V1 "KAIPLAYER-CATALOG 1"  // Without the times.
#define CACHE_FIELDS 10
#define CACHE_LINE_MAX (UTIL_PATH_MAX + 5 * TAGS_TEXT_MAX + 64)

void catalogInit(Catalog *c)
{
    memset(c, 0, sizeof(*c));
}

static void freeGroups(Catalog *c)
{
    for (size_t i = 0; i < c->album_count; i++)
        free(c->albums[i].songs);
    for (size_t i = 0; i < c->artist_count; i++)
        free(c->artists[i].albums);
    free(c->albums);
    free(c->artists);
    free(c->songs_by_title);
    c->albums = NULL;
    c->artists = NULL;
    c->songs_by_title = NULL;
    c->album_count = c->artist_count = 0;
}

void catalogFree(Catalog *c)
{
    freeGroups(c);
    for (size_t i = 0; i < c->song_count; i++) {
        CatalogSong *s = &c->songs[i];
        free(s->path);
        free(s->title);
        free(s->artist);
        free(s->album_artist);
        free(s->album);
    }
    free(c->songs);
    catalogInit(c);
}

// strdup with tabs and line breaks turned into spaces, so the cache file
// keeps one song per line.
static char *copyClean(const char *s)
{
    char *copy = strdup(s ? s : "");
    for (char *p = copy; p && *p; p++) {
        if (*p == '\t' || *p == '\n' || *p == '\r')
            *p = ' ';
    }
    return copy;
}

bool catalogAddFields(Catalog *c, const CatalogSong *f)
{
    if (strpbrk(f->path, "\t\r\n"))
        return true;  // Such a name can't be kept in the cache file: left out.
    if (c->song_count == c->song_capacity) {
        size_t grown = c->song_capacity ? c->song_capacity * 2 : 256;
        CatalogSong *songs = realloc(c->songs, grown * sizeof(CatalogSong));
        if (!songs)
            return false;
        c->songs = songs;
        c->song_capacity = grown;
    }
    CatalogSong s = {
        .path = strdup(f->path), .title = copyClean(f->title), .artist = copyClean(f->artist),
        .album_artist = copyClean(f->album_artist), .album = copyClean(f->album),
        .track = f->track, .disc = f->disc, .year = f->year, .size = f->size, .modified = f->modified,
    };
    if (!s.path || !s.title || !s.artist || !s.album_artist || !s.album) {
        free(s.path), free(s.title), free(s.artist), free(s.album_artist), free(s.album);
        return false;
    }
    c->songs[c->song_count++] = s;
    return true;
}

// The name of the folder `levels` above path's file, if it is still inside
// root; "" otherwise. Level 1 is the folder holding the file.
static void folderName(const char *path, const char *root, int levels, char *out, size_t size)
{
    char folder[UTIL_PATH_MAX];
    snprintf(folder, sizeof(folder), "%s", path);
    out[0] = '\0';
    for (int i = 0; i < levels; i++) {
        if (!utilParentPath(folder))
            return;
    }
    // The music folder itself names nothing: its songs have no album folder.
    if (!utilPathWithin(folder, root) || utilPathWithin(root, folder))
        return;
    snprintf(out, size, "%s", utilFileName(folder));
}

bool catalogAdd(Catalog *c, const char *path, long size, const TrackTags *t, const char *root)
{
    return catalogAddFile(c, path, size, 0, t, root);
}

bool catalogAddFile(Catalog *c, const char *path, long size, int64_t modified, const TrackTags *t, const char *root)
{
    char title[UTIL_PATH_MAX], album[UTIL_PATH_MAX], artist_folder[UTIL_PATH_MAX];
    if (t->title[0]) {
        snprintf(title, sizeof(title), "%s", t->title);
    } else {
        snprintf(title, sizeof(title), "%s", utilFileName(path));
        char *dot = strrchr(title, '.');
        if (dot && dot != title)
            *dot = '\0';
    }
    if (t->album[0])
        snprintf(album, sizeof(album), "%s", t->album);
    else
        folderName(path, root, 1, album, sizeof(album));
    folderName(path, root, 2, artist_folder, sizeof(artist_folder));  // "Artist/Album/song".
    if (!t->album[0] && !album[0])
        snprintf(album, sizeof(album), "%s", "Unknown album");

    const char *group = t->album_artist[0] ? t->album_artist
                      : t->artist[0]       ? t->artist
                      : !t->album[0] && artist_folder[0] ? artist_folder
                                                         : CATALOG_UNKNOWN_ARTIST;
    CatalogSong f = {
        .path = (char *)path, .title = title, .artist = (char *)(t->artist[0] ? t->artist : group),
        .album_artist = (char *)group, .album = album,
        .track = t->track_number, .disc = t->disc_number, .year = t->year, .size = size, .modified = modified,
    };
    return catalogAddFields(c, &f);
}

// --- Building ---

static const Catalog *g_sorting;  // qsort has no context argument.

static int byPath(const void *a, const void *b)
{
    return strcmp(((const CatalogSong *)a)->path, ((const CatalogSong *)b)->path);
}

static int byTitle(const void *a, const void *b)
{
    const CatalogSong *x = &g_sorting->songs[*(const size_t *)a], *y = &g_sorting->songs[*(const size_t *)b];
    int d = utilCompareNames(x->title, y->title);
    return d ? d : strcmp(x->path, y->path);
}

// Album identity: album artist and title, ignoring case.
static int songAlbumOrder(const CatalogSong *x, const CatalogSong *y)
{
    int d = utilCompareNames(x->album_artist, y->album_artist);
    return d ? d : utilCompareNames(x->album, y->album);
}

static int byAlbumThenTrack(const void *a, const void *b)
{
    const CatalogSong *x = &g_sorting->songs[*(const size_t *)a], *y = &g_sorting->songs[*(const size_t *)b];
    int d = songAlbumOrder(x, y);
    if (d)
        return d;
    if (x->disc != y->disc)
        return x->disc - y->disc;
    if (x->track != y->track)
        return x->track - y->track;
    return utilCompareNames(x->path, y->path);
}

static int albumByTitle(const void *a, const void *b)
{
    const CatalogAlbum *x = a, *y = b;
    int d = utilCompareNames(x->title, y->title);
    return d ? d : utilCompareNames(g_sorting->songs[x->songs[0]].album_artist,
                                    g_sorting->songs[y->songs[0]].album_artist);
}

static int albumByYear(const void *a, const void *b)
{
    const CatalogAlbum *x = &g_sorting->albums[*(const size_t *)a], *y = &g_sorting->albums[*(const size_t *)b];
    if (x->year != y->year)
        return x->year - y->year;
    return utilCompareNames(x->title, y->title);
}

// Groups the songs, already in album order, into albums.
static bool makeAlbums(Catalog *c, const size_t *order)
{
    c->albums = calloc(c->song_count ? c->song_count : 1, sizeof(CatalogAlbum));
    if (!c->albums)
        return false;
    for (size_t i = 0; i < c->song_count;) {
        size_t end = i + 1;
        while (end < c->song_count && songAlbumOrder(&c->songs[order[i]], &c->songs[order[end]]) == 0)
            end++;
        CatalogAlbum *a = &c->albums[c->album_count++];
        a->songs = malloc((end - i) * sizeof(size_t));
        if (!a->songs)
            return false;
        memcpy(a->songs, order + i, (end - i) * sizeof(size_t));
        a->song_count = end - i;
        a->title = c->songs[order[i]].album;
        for (size_t k = i; k < end; k++)
            a->year = c->songs[order[k]].year > a->year ? c->songs[order[k]].year : a->year;
        i = end;
    }
    g_sorting = c;
    qsort(c->albums, c->album_count, sizeof(CatalogAlbum), albumByTitle);
    for (size_t a = 0; a < c->album_count; a++)
        for (size_t k = 0; k < c->albums[a].song_count; k++)
            c->songs[c->albums[a].songs[k]].album_index = a;
    return true;
}

static int albumIndexByArtist(const void *a, const void *b)
{
    const CatalogAlbum *x = &g_sorting->albums[*(const size_t *)a], *y = &g_sorting->albums[*(const size_t *)b];
    int d = utilCompareNames(g_sorting->songs[x->songs[0]].album_artist, g_sorting->songs[y->songs[0]].album_artist);
    return d ? d : albumByYear(a, b);
}

// Groups the albums into artists, by album artist.
static bool makeArtists(Catalog *c)
{
    size_t *order = malloc((c->album_count ? c->album_count : 1) * sizeof(size_t));
    c->artists = calloc(c->album_count ? c->album_count : 1, sizeof(CatalogArtist));
    if (!order || !c->artists) {
        free(order);
        return false;
    }
    for (size_t i = 0; i < c->album_count; i++)
        order[i] = i;
    g_sorting = c;
    qsort(order, c->album_count, sizeof(size_t), albumIndexByArtist);
    bool ok = true;
    for (size_t i = 0; i < c->album_count && ok;) {
        const char *name = c->songs[c->albums[order[i]].songs[0]].album_artist;
        size_t end = i + 1;
        while (end < c->album_count &&
               utilCompareNames(c->songs[c->albums[order[end]].songs[0]].album_artist, name) == 0)
            end++;
        CatalogArtist *ar = &c->artists[c->artist_count];
        ar->albums = malloc((end - i) * sizeof(size_t));
        if (!(ok = ar->albums != NULL))
            break;
        memcpy(ar->albums, order + i, (end - i) * sizeof(size_t));
        ar->album_count = end - i;
        ar->name = (char *)name;
        for (size_t k = i; k < end; k++) {
            c->albums[order[k]].artist = c->artist_count;
            ar->song_count += c->albums[order[k]].song_count;
        }
        c->artist_count++;
        i = end;
    }
    free(order);
    return ok;
}

bool catalogBuild(Catalog *c)
{
    freeGroups(c);
    if (c->song_count > 1)
        qsort(c->songs, c->song_count, sizeof(CatalogSong), byPath);
    size_t *order = malloc((c->song_count ? c->song_count : 1) * sizeof(size_t));
    c->songs_by_title = malloc((c->song_count ? c->song_count : 1) * sizeof(size_t));
    if (!order || !c->songs_by_title) {
        free(order);
        freeGroups(c);
        return false;
    }
    for (size_t i = 0; i < c->song_count; i++)
        order[i] = c->songs_by_title[i] = i;
    g_sorting = c;
    qsort(order, c->song_count, sizeof(size_t), byAlbumThenTrack);
    qsort(c->songs_by_title, c->song_count, sizeof(size_t), byTitle);
    bool ok = makeAlbums(c, order) && makeArtists(c);
    free(order);
    if (!ok)
        freeGroups(c);
    return ok;
}

long catalogFind(const Catalog *c, const char *path)
{
    size_t lo = 0, hi = c->song_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int d = strcmp(c->songs[mid].path, path);
        if (d == 0)
            return (long)mid;
        if (d < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

// --- Cache file ---

bool catalogSave(const Catalog *c, FILE *f)
{
    fprintf(f, "%s\n", CACHE_HEADER);
    for (size_t i = 0; i < c->song_count; i++) {
        const CatalogSong *s = &c->songs[i];
        fprintf(f, "%ld\t%lld\t%d\t%d\t%d\t%s\t%s\t%s\t%s\t%s\n", s->size, (long long)s->modified, s->track, s->disc,
                s->year, s->path, s->title, s->artist, s->album_artist, s->album);
    }
    return !ferror(f);
}

// Splits line at tabs into at most n fields. Returns how many it found.
static int splitTabs(char *line, char **fields, int n)
{
    int count = 0;
    fields[count++] = line;
    for (char *p = line; *p && count < n; p++) {
        if (*p == '\t') {
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    return count;
}

bool catalogLoad(Catalog *c, FILE *f)
{
    char *line = malloc(CACHE_LINE_MAX);
    if (!line)
        return false;
    bool ok = fgets(line, CACHE_LINE_MAX, f) != NULL;
    bool v1 = ok && strncmp(line, CACHE_HEADER_V1, strlen(CACHE_HEADER_V1)) == 0;
    bool v2 = ok && strncmp(line, CACHE_HEADER_V2, strlen(CACHE_HEADER_V2)) == 0;
    ok = ok && (v1 || v2 || strncmp(line, CACHE_HEADER, strlen(CACHE_HEADER)) == 0);
    int fields = v1 ? CACHE_FIELDS - 1 : CACHE_FIELDS;
    while (ok && fgets(line, CACHE_LINE_MAX, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *field[CACHE_FIELDS], **rest = field + (v1 ? 1 : 2);  // After the size and the time.
        if (splitTabs(line, field, fields) != fields || !rest[3][0])
            continue;  // A damaged line: that song is read again from the file.
        if ((v1 || v2) && (utilHasExtension(rest[3], "ogg") || utilHasExtension(rest[3], "oga")))
            continue;  // Perhaps Opus, whose tags weren't read then: read again.
        CatalogSong s = {
            .size = strtol(field[0], NULL, 10), .modified = v1 ? 0 : strtoll(field[1], NULL, 10),
            .track = atoi(rest[0]), .disc = atoi(rest[1]), .year = atoi(rest[2]), .path = rest[3],
            .title = rest[4], .artist = rest[5], .album_artist = rest[6], .album = rest[7],
        };
        ok = catalogAddFields(c, &s);
    }
    free(line);
    return ok;
}
