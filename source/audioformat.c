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

void decoderFormatLabel(const AudioFormat *f, char *out, size_t size)
{
    out[0] = '\0';
    if (!f->codec[0])
        return;
    bool dsd = strncmp(f->codec, "DSD", 3) == 0;
    char rate[16];
    const char *unit;
    rateText(f->sample_rate, rate, sizeof(rate), &unit);
    if (f->bits && !dsd)
        snprintf(out, size, "%s \xC2\xB7 %u-bit \xC2\xB7 %s %s", f->codec, f->bits, rate, unit);
    else if (f->kbps)
        snprintf(out, size, "%s \xC2\xB7 %u kbps", f->codec, f->kbps);
    else
        snprintf(out, size, "%s \xC2\xB7 %s %s", f->codec, rate, unit);
}
