# The library

[Back to the README](../README.md)

KaiPlayer builds a library of your artists, albums and songs from the tags of
the music in your music folder. It only shows that folder and the ones inside
it. You choose the folder the first time you open KaiPlayer (or later), and can
change it in **+ → Settings → Library → Music folder**. Without one, the SD
card's library is the whole card, and the pill says **SD card (whole card)**.

The library can also come from a [network folder](network-folder.md) or a
[Navidrome server](navidrome.md): with one set up, the pill next to
KaiPlayer's name says which is shown. **X** on Home or on Library's first
page, a tap on the pill, or **+ → Go to the … library** goes to the next one
set up: **SD card**, **network folder**, **Navidrome**, and back.

## Home

**Home** is the first tab: four sections of up to four cards each, with
their covers, all on one screen:

| | |
|---|---|
| **Your top artists** (the ones you play most) | **Recent playlists** |
| **Recent albums** | **Recent artists** |

- Before you have played anything, it shows albums from your library.
- Use the D-pad or the left stick to move between cards and sections, **A**
  to open one, **X** to play it and **Y** to shuffle it, or tap a card. (With a
  network folder or a server set up, **X** switches the library instead: see
  above.)
- A song counts as played after 30 seconds (half of a song shorter than a
  minute).
- The plays are kept in `history.txt` in KaiPlayer's folder; delete that file
  to start over.

## Library

**Library** organizes your music folder like a music app:

- **Artists:** each artist's albums, newest last. **X** plays everything by
  the artist.
- **Albums:** every album, with its cover. Its songs go in track order.
- **Songs:** every song by title.
- **Folders:** the folders of your music folder, as they are on the card (or
  in the network folder).

Artists and albums come from the tags (album artist, artist, album, track
and disc numbers, year), **not from the folder names**: a folder named
`Miki Matsubara` whose songs are tagged 松原みき shows as **松原みき**. Songs
without tags are grouped by folder, as `Artist/Album/song.mp3`.

KaiPlayer keeps the library in `catalog.txt` in its folder so it shows at
once next time, and updates it in the background, reading only new or changed
files; the progress shows at the top right. **+ → Update the library** (or
**+ → Settings → Library → Update the library**) checks the folder again after
you add music or edit tags. A file counts as changed when its size or its
modified time is different; if the console doesn't give the card's files
real times, only the size counts, so a tag edit that keeps the file's size
isn't noticed.

## Playing folders

**X** and **Y** also play the subfolders. When you press them in the root of
the card, KaiPlayer skips the system and homebrew folders there (`switch`,
`config`, `Nintendo`, `atmosphere`, kaiHEN's folders and so on), so their
sound effects don't end up in the queue.

## Song rows

Song rows in Library, Playlists and Queue show the song's **title**,
**Artist** and **Album** from its tags, like Now Playing.

- They are read in the background, so they appear a moment after a list
  opens.
- Until then, and for songs without tags, the row shows the file name (and
  the folder name in grey instead of the album).
- The song that is playing has moving bars next to its name (still while
  paused).

## Lyrics

Now Playing shows a song's lyrics in the place of the album, with a **Lyrics**
button: tap it, or press **↑**, to show or hide them (KaiPlayer remembers your
choice). They come from, in this order:

1. A `.lrc` file next to the song with the same name (`Song.flac` and
   `Song.lrc`), on the card or in a network folder.
2. The song's own tags: `USLT` in MP3, `LYRICS` or `UNSYNCEDLYRICS` in FLAC and
   Ogg, the lyrics of an M4A.
3. For a song from a Navidrome server, the server (its `getLyricsBySongId`, which
   gives the lyrics it found in the song or next to it).

**Synced lyrics** have the time each line starts: the line being sung shows in
the middle, the lines before and after it sliding up. **Plain lyrics**, without
times, scroll evenly from the start of the song to its end.

The `.lrc` format is plain text with the time each line starts:

```
[00:12.30]First line
[00:17.85]Second line
[00:40.00][01:52.10]A chorus sung twice
```

`[offset:+500]` shows every line half a second sooner. A file with no times in
it is shown as plain lyrics.

## Song details and album art

Now Playing shows the album cover, title, artist and album. The mini player
at the bottom of Home, Library, Playlists and Queue shows a small cover, the
title and the artist.

KaiPlayer reads the song details from the file's tags:

| Format     | Tags read |
|------------|-----------|
| MP3        | ID3v2.2, v2.3 and v2.4 (title, artist, album, cover); ID3v1 as a fallback |
| FLAC       | Vorbis comments and the embedded picture |
| Ogg Vorbis | Vorbis comments and the embedded picture (`METADATA_BLOCK_PICTURE`) |
| Opus       | The same, read with FFmpeg; ReplayGain also from `R128_TRACK_GAIN`/`R128_ALBUM_GAIN` |
| AAC, ALAC (`.m4a`, `.mp4`) | iTunes-style MP4 tags, ReplayGain from `----` (`replaygain_*`) tags, the cover (`covr`), read with FFmpeg |
| AAC (`.aac`) | An ID3v2 tag at the start, if it has one |
| WAV        | None |
| DSF        | ID3v2 (the tag at the end of the file) |
| DSDIFF     | ID3v2 in an `ID3 ` chunk, if the file has one |

When a file has no title tag, KaiPlayer shows its file name without the
extension. When it has no artist tag, Now Playing shows "Unknown artist" and
the mini player shows the name of the song's folder.

KaiPlayer looks for the cover in this order:

1. **The cover embedded in the file.** If a file has several pictures,
   KaiPlayer prefers the front cover.
2. **An image in the song's folder** named `cover`, `folder`, `front` or
   `albumart…`, in that order. Letter case doesn't matter (`Cover.jpg` and
   `FOLDER.PNG` work), and the image can be `.jpg`, `.jpeg` or `.png`.
3. **The folder's only image**, whatever its name, if the folder has exactly
   one image file.

Covers can be JPEG or PNG, up to 4096×4096 pixels and 8 MB. For a folder of
FLAC files without embedded art, uploading the album's `Cover.jpg` next to
them is enough.

Text in tags can be in any language; file and folder names can't. See
[File names and tags](file-names.md).
