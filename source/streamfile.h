// The Internet Archive's songs and covers for kfile.c (KFileRemote), as subfile.c reads the server's: songs
// with HTTP ranges from archive.org/download (streamFileUrl), covers read whole. The console's network must
// be started (netInit) before kfile is given these.
#pragma once

#include "kfile.h"

const KFileRemote *streamFileRemote(void);
// Closes the connection kept for small reads (KaiPlayer ends).
void streamFileForget(void);
