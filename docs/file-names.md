# File names and tags

[Back to the README](../README.md)

## Any language in tags

Text in tags and file names can be in any language:

- **Latin, Greek and Cyrillic** are drawn with KaiPlayer's own font (Noto
  Sans).
- **Japanese (kanji and kana), Chinese and Korean** are drawn with the fonts
  built into the console.

## File and folder names must be plain ASCII

**File and folder names, however, must use plain ASCII letters.** This is a
limitation of the console, not of KaiPlayer:

- On Switch 2, a file or folder whose name has any non-ASCII character
  (accented letters such as á, é, ñ or ü, typographic quotes such as ’, or
  Japanese) is written correctly.
- But the console's file system leaves it out of the folder: it isn't listed,
  and it isn't even counted among the folder's entries.
- FTP listings, OunceShell and KaiPlayer all see folders through it, so the
  file is invisible in all of them, even though it is on the card and takes
  up space.
- No app on the console can find it, so the fix has to happen before the file
  gets there.

## Tags carry the real names

KaiPlayer takes each song's title, artist and album from its tags (ID3 in MP3
and DSD, Vorbis comments in FLAC, Ogg and Opus, MP4 tags in `.m4a`), not from
the file name, and tags
can hold any language. A file renamed `04 Yoru no Minato.mp3` is still shown
as **夜の港 · 夜灯楽団 · 夜灯り**, with its embedded cover, and
`06 Cancion.flac` as **Canción**.

Make sure your songs are tagged (most music from stores or ripped with a good
tool is): a song without tags shows its file name, and is grouped by its
folders, so with a plain file name it shows without its accents.

## Upload with plain names, without renaming your files

FileZilla can't rename files as it uploads them.

### KaiDrop

Use [KaiDrop](https://github.com/mizuiroame/kaidrop) (work in progress), a
small Windows app (also runs on macOS and Linux) that sends folders over FTP
and gives the copies on the console plain names, with a preview of every
name.

### kaiupload.py

Or, from the command line, the repository includes a small script,
[`tools/kaiupload.py`](../tools/kaiupload.py) (Python 3.8 or later, nothing
to install). It uploads a folder over FTP and gives the copies on the console
plain names; the files on your computer are not changed:

```
python kaiupload.py 192.168.1.50 "G:\Music\Spanish album" /music --port 5000
```

- Use the IP address and port that OunceShell's FTP screen shows (the same
  ones you use in FileZilla); add `--user` and `--password` if your FTP
  server asks for them.
- Accents are dropped (`06 Canción.mp3` → `06 Cancion.mp3`, `Ñandú` →
  `Nandu`), and characters such as Japanese become `_`
  (`04 夜の港.mp3` → `04 _.mp3`).
- Run it with `--dry-run` first to see the names it will use.
- Files already on the console with the same size are skipped, so you can run
  it again after adding songs or if an upload stops.

## Fixing a file that is already hidden

If you already uploaded a file with such a name, it is on the card but
hidden. You can rename or delete it with FileZilla's
**Server → Enter custom command** (use the folder that holds it as the
current remote folder):

```
RNFR 04 夜の港.mp3
RNTO 04 Yoru no Minato.mp3
```

or, to delete it:

```
DELE 04 夜の港.mp3
```

`SIZE <name>` tells you whether a hidden file is there: it answers `213`
followed by the file size.
