// The Navidrome server's songs and covers for kfile.c (KFileRemote): songs
// read with HTTP ranges from "download" (or "stream" with format=raw when the
// server doesn't let its songs be downloaded), covers from "getCoverArt",
// read whole. The console's network must be started (netInit) before kfile
// is given these.
#pragma once

#include "kfile.h"

const KFileRemote *subFileRemote(void);
// Closes the connection kept for small reads (the account changed, or KaiPlayer ends).
void subFileForget(void);
