// The Internet Archive (stream.h) over the network: its pages asked for on a thread of their own. The
// console's network must be started (netInit) before anything here asks it.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "stream.h"

#define STREAM_WHY_MAX 256

// The pages' thread. Start before streamFetch; stop at the end (a request on its way is called off).
bool streamNetStart(void);
void streamNetStop(void);
// Asks for q on the thread; a newer request calls off the one waiting or on its way. Its serial
// (never 0), or 0 if it couldn't be asked for.
unsigned streamFetch(const StreamQuery *q);
// The answer that came, if one did: its serial, whether it worked (else why), and its list (taken:
// free it). False while there's none.
bool streamFetchTake(unsigned *serial, bool *ok, StreamList *list, char *why, size_t why_size);
