#include "audioformat.h"

#include <stdio.h>
#include <string.h>

// 44100 as "44.1", 48000 as "48", 2822400 as "2.8" (in MHz).
static void rateText(uint32_t hz, char *out, size_t size, const char **unit)
{
    bool mega = hz >= 1000000;
    *unit = mega ? "MHz" : "kHz";
    if (!mega && hz % 1000 == 0)
        snprintf(out, size, "%u", (unsigned)(hz / 1000));
    else
        snprintf(out, size, "%.1f", mega ? hz / 1e6 : hz / 1e3);
}

void decoderFormatLabel(const AudioFormat *f, bool compact, char *out, size_t size)
{
    out[0] = '\0';
    if (!f->codec[0])
        return;
    bool dsd = strncmp(f->codec, "DSD", 3) == 0;
    char rate[16];
    const char *unit;
    rateText(f->sample_rate, rate, sizeof(rate), &unit);
    if (compact && dsd)
        snprintf(out, size, "%s", f->codec);
    else if (compact && f->bits)
        snprintf(out, size, "%s %u/%s", f->codec, f->bits, rate);
    else if (compact)
        snprintf(out, size, "%s %s", f->codec, rate);
    else if (f->bits && !dsd)
        snprintf(out, size, "%s \xC2\xB7 %u-bit \xC2\xB7 %s %s", f->codec, f->bits, rate, unit);
    else
        snprintf(out, size, "%s \xC2\xB7 %s %s", f->codec, rate, unit);
}
