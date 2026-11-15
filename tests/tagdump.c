// Prints what KaiPlayer reads from audio files: tags, cover and stream
// info, through the same code as the .nro. A developer tool for checking
// real files on the host.
// Usage: tagdump FILE...
#include <stdio.h>

#include "cover.h"
#include "decoder.h"
#include "tags.h"

#define LARGE 400
#define SMALL 56

static void dump(const char *path)
{
    printf("%s\n", path);
    Decoder *d = decoderOpen(path);
    if (d) {
        printf("  audio:  %u Hz, %llu frames\n", decoderSampleRate(d),
               (unsigned long long)decoderTotalFrames(d));
        decoderClose(d);
    } else {
        printf("  audio:  cannot decode\n");
    }

    TrackTags t;
    tagsInit(&t);
    tagsRead(path, &t);
    printf("  title:  %s\n  artist: %s\n  album:  %s\n", t.title, t.artist, t.album);
    CoverImage large, small;
    if (t.picture && coverDecode(t.picture, t.picture_size, &large, LARGE, &small, SMALL)) {
        printf("  cover:  embedded, %zu bytes, type %d, decoded\n", t.picture_size, t.picture_type);
    } else if (coverLoadFromFolder(path, &large, LARGE, &small, SMALL)) {
        printf("  cover:  from the folder, decoded\n");
    } else {
        printf("  cover:  none%s\n", t.picture ? " (embedded picture failed to decode)" : "");
    }
    coverFree(&large);
    coverFree(&small);
    tagsFree(&t);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: tagdump FILE...\n");
        return 2;
    }
    for (int i = 1; i < argc; i++)
        dump(argv[i]);
    return 0;
}
