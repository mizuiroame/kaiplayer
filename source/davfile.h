// The network folder's files for kfile.c (KFileRemote): read with SMB, or
// from WebDAV with HTTP ranges. The console's network must be started
// (netInit) before kfile is given these.
#pragma once

#include "kfile.h"

const KFileRemote *davFileRemote(void);
// Closes the connection kept for WebDAV's small reads (the account changed, or KaiPlayer ends).
void davFileForget(void);
