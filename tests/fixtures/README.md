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

navidrome_search3.json is what a Navidrome test server answered to `search3`
(the Subsonic API's search with an empty query) for eight made-up songs; the
fuzzer starts from it.
