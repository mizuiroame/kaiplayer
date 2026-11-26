# Test fixtures

Small files made for the tests. They contain no third-party content: a
1-second 440 Hz sine tone and a generated 64×64 image.

tone.dsf and tone.dff (0.2 s of DSD64: 440 Hz left, 880 Hz right) are made
by `python make_dsd.py`, with no packages needed.

The others were made with ffmpeg and Pillow:

```sh
# small_cover.jpg: a pink square with a yellow circle (Pillow).
ffmpeg -f lavfi -i "sine=frequency=440:duration=1:sample_rate=44100" -i small_cover.jpg \
  -map 0:a -map 1 -ac 2 -c:a libmp3lame -b:a 64k -id3v2_version 3 -c:v copy \
  -disposition:v attached_pic -metadata title="Test Title" \
  -metadata artist="Tëst Ártist" -metadata album="Fixture Album" tone.mp3
# tone.flac: the same with -c:a flac.
# tone.ogg: -c:a libvorbis -q:a 0, with the cover as a base64 FLAC picture
# block in METADATA_BLOCK_PICTURE (ffmpeg's Ogg muxer can't attach pictures).
```

tone.m4a (AAC, with tags, ReplayGain and the cover), tone_alac.m4a (ALAC,
24-bit, 96 kHz), tone.opus (Opus with tags, R128 gain and the cover),
tone_opus.ogg (mono Opus in an .ogg file) and tone.aac (ADTS, no tags) are
made by `python make_ffmpeg_tones.py`, with ffmpeg in Docker.

navidrome_search3.json is what a Navidrome test server answered to `search3`
(the Subsonic API's search with an empty query) for eight made-up songs; the
fuzzer starts from it.

tone_book.m4b (3 s of a 330 Hz tone, AAC, the cover, and three chapters in a
QuickTime chapter track) and tone_book_chapters.mp3 (the same as MP3 with ID3v2.3
CTOC/CHAP frames) are audiobook fixtures, made with ffmpeg from an
FFMETADATA file with three [CHAPTER] blocks ("Opening", "The Middle",
"Ending", one second each):

```sh
ffmpeg -f lavfi -i "sine=frequency=330:duration=3:sample_rate=22050" -i chapters.txt -i small_cover.jpg \
  -map 0:a -map 2:v -map_metadata 1 -map_chapters 1 -c:a aac -b:a 24k -ac 1 -c:v copy \
  -disposition:v attached_pic -f ipod tone_book.m4b
ffmpeg -f lavfi -i "sine=frequency=330:duration=3:sample_rate=22050" -i chapters.txt \
  -map 0:a -map_metadata 1 -map_chapters 1 -c:a libmp3lame -b:a 32k -ac 1 -id3v2_version 3 tone_book_chapters.mp3
```

jamendo_*.json and archive_*.json are made-up answers in the shape of
Jamendo's API v3.0 (`tracks`, `albums`, `artists`, an error, a track's audio
address) and the Internet Archive's `advancedsearch.php` and `metadata/<id>`
(an etree concert, a Creative Commons netlabel release). The bands, songs,
ids and addresses are invented, and a few entries are broken on purpose (an
id that isn't one, a file name with a control character). No real service is
asked in the tests; the fuzzer starts from these too.
