# Audiobooks

The **Audiobooks** tab (between Library and Playlists) lists your books, the
ones you're in the middle of first, with how much is left of each.

## Where books come from

- **`sdmc:/audiobooks`** on the SD card: a folder for each book (its files play
  in name order, `Chapter 2` before `Chapter 10`; `CD 1`, `CD 2`... folders
  inside count as one book), or `.m4b` files. A folder per author above the
  books is fine: `sdmc:/audiobooks/Author/Book/`.
- **Your library** (the card's music folder, the network folder or Navidrome):
  every `.m4b`, and the files in a folder named `Audiobooks` (also
  `Hörbücher`, `Audiolibros`, `Livres audio`, `Audiolibri`, `Audiolivros`...).
- **The network folder**: a folder named `Audiobooks` at its top.
- **Navidrome** (or another Subsonic server): the albums of a genre named
  `Audiobook` or `Audiobooks`, and of a library (music folder) with that name.
  Each album is a book.

A book's name and author come from its tags (album and album artist), else
from its folders. Its cover is the album's, as in the Library.

## Chapters

- An `.m4b` (or `.m4a`) with chapters: its own chapters.
- An MP3 with ID3 chapters (`CHAP` frames), or with a `.cue` sheet of the same
  name next to it: those.
- Anything else: each file is a chapter, named after its title tag.

## Playing a book

Choose a book and press **A**: it goes on where you left it, in its own
place, apart from the music's resume. Playing music in between doesn't move
it, and the book that was playing when KaiPlayer closed opens again.

| Button | Does |
| --- | --- |
| A | Play / pause |
| ← → or the right stick | 30 seconds back / on |
| L / R | Previous / next chapter (a few seconds into a chapter, L goes to its start) |
| X | The chapters |
| Y | Reading speed: 0.75x to 2x, the voice's pitch kept |
| ↑ | Sleep timer: minutes, or the end of the chapter |
| B | Back to the Audiobooks tab |

Every one of these is also on screen, for touch. The chapter's bar moves within
the chapter; the thin bar below it is the whole book, with the time left at the
speed you chose.

In the tab, **X** shows a book's chapters (to start at one), and **Y** its
options: start from the beginning, or mark it as finished or not.

A Navidrome book also tells the server where you are (its bookmarks), when you
pause and every minute, and goes on from the server's place when the book was
played later on another player.
