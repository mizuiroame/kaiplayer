// One reader for the music files, on the card, in the network folder
// ("dav:/" locations, dav.h) or on a Navidrome server ("sub:/", subsonic.h):
// open, read, seek, tell, size, close. The
// decoders, the tags, the DSD reader and the covers read through it.
//
// A card file is read with stdio, as before. A network file comes in blocks:
// a stream (a song playing) is read ahead on a thread of its own, so the
// sound seldom waits for the network; a peek (tags, a cover) reads only the
// blocks asked for, on the caller's thread. kfile.c knows blocks, not
// servers: davfile.c gives it the network folder's reads (KFileRemote), and
// subfile.c the Navidrome server's.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "dav.h"

typedef enum {
    KFILE_PEEK,    // A few reads: tags, a cover.
    KFILE_STREAM,  // Read through: a song playing.
} KFileMode;

typedef struct KFile KFile;

// How network files are read. Every function may be called from any thread,
// one file by one thread at a time.
typedef struct {
    // Opens a location: stream for a song to play (its own connection if it
    // needs one). *size gets its length. NULL if it can't be opened.
    void *(*open)(const char *location, bool stream, int64_t *size);
    // Reads size bytes at offset (fewer only at the end), checking cancel
    // while it waits. How many, or -1 if it failed or was cancelled.
    int64_t (*read)(void *file, int64_t offset, void *to, size_t size, const volatile bool *cancel);
    void (*close)(void *file);
    // Lists a folder ("dav:/" or "dav:/Albums"). *out is malloc'd. False with why (for the person).
    // (A server without folders always says why not.)
    bool (*list)(const char *location, DavEntry **out, size_t *count, char *why, size_t why_size);
} KFileRemote;

typedef enum {
    KFILE_REMOTE_FOLDER,  // The network folder's "dav:/" locations.
    KFILE_REMOTE_SERVER,  // The Navidrome server's "sub:/" locations.
    KFILE_REMOTE_STREAM,  // The Internet Archive's "ia:/" locations (stream.h).
    KFILE_REMOTE_COUNT,
} KFileRemoteKind;

// What that kind's files are read with (kept, not copied); NULL: they can't be opened.
void kfileSetRemote(KFileRemoteKind kind, const KFileRemote *remote);

// True for a network folder's or a server's location.
bool kfileIsRemote(const char *path);

// NULL if it can't be opened.
KFile *kfileOpen(const char *path, KFileMode mode);
void kfileClose(KFile *f);
// Reads up to size bytes; fewer at the end, or when the network failed (kfileFailed).
size_t kfileRead(KFile *f, void *to, size_t size);
// whence is SEEK_SET, SEEK_CUR or SEEK_END. False if it can't (before the start).
bool kfileSeek(KFile *f, int64_t offset, int whence);
int64_t kfileTell(const KFile *f);
// The file's length in bytes; -1 if it can't be known.
int64_t kfileSize(KFile *f);
// True once a read came short because the network failed (not the end).
bool kfileFailed(const KFile *f);
// A card file's FILE, for a library that reads one itself; NULL for a network file.
FILE *kfileStdio(KFile *f);

// A whole file of at most max bytes, in one read (one request over the
// network). malloc'd (free it), *size its length; NULL if it can't be read
// or is larger. failed (may be NULL) is set if the reason was the network.
uint8_t *kfileReadAll(const char *path, size_t max, size_t *size, bool *failed);

// Lists a network folder through the remote. False with why if it can't.
bool kfileList(const char *location, DavEntry **out, size_t *count, char *why, size_t why_size);

// Streams opened before this stop waiting for the network: their reads come
// back short. The player calls it when the song changes, so it never waits
// for a server that has gone. Peeks go on.
void kfileInterruptStreams(void);

// For the tests: blocks this small (0: the usual), so that small files cross them.
// Before any file is opened.
void kfileSetBlockSizes(size_t stream, size_t peek);
