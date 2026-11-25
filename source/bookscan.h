// Finds the audiobooks on a background thread (books.h): the files in
// sdmc:/audiobooks, the library's files that are books (an .m4b, or under a
// folder named "Audiobooks"...), the network folder's books folder, and the
// Navidrome server's books (booksub.h). Each book is then read once for its
// files' lengths, its chapters and its names, and kept in books.cache in
// KaiPlayer's folder, so the tab fills at once the next time.
// Another thread tells the server where a book was left (its bookmarks).
#pragma once

#include <stdbool.h>

#include "books.h"

#define BOOKSCAN_CARD_FOLDER "sdmc:/audiobooks"

bool bookScanInit(void);
void bookScanExit(void);

// Looks again: network says the network folder can be read, server the Navidrome server.
// Cheap to call; a look running starts over.
void bookScanStart(bool network, bool server);

// The books found (sorted by title); hold the lock while reading them.
const BookList *bookScanLock(void);
void bookScanUnlock(void);
// Changes whenever the list or a book in it changes. Without the lock: may be asked while it's held.
unsigned bookScanGeneration(void);
// True from the start until a whole look has ended, and while looking for books or reading them.
// Without the lock, as above.
bool bookScanBusy(void);

// Tells the server a book is at seconds into this song of its (a "sub:/" location): in the background,
// the newest asked for wins.
void bookScanSendBookmark(const char *location, double seconds);
