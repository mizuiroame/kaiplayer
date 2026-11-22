# Vendored third-party code and fonts

These files are copied unmodified from the upstream commits below. Run
`sh third_party/verify.sh` to check them against the SHA-256 sums.

To update a dependency, download the files from a newer commit, then update
the commit, version and SHA-256 in this file and in `verify.sh`. Review the
upstream changes before you update: the decoders parse untrusted files.

| File | Upstream | Commit | Version | License |
|------|----------|--------|---------|---------|
| `dr_mp3.h` | [mackron/dr_libs](https://github.com/mackron/dr_libs) | `f12e197fd462e4c22873fbd70d11b1d2f2c1dc3c` | 0.7.5 | Public domain or MIT-0 |
| `dr_flac.h` | [mackron/dr_libs](https://github.com/mackron/dr_libs) | `f12e197fd462e4c22873fbd70d11b1d2f2c1dc3c` | 0.13.5 | Public domain or MIT-0 |
| `dr_wav.h` | [mackron/dr_libs](https://github.com/mackron/dr_libs) | `f12e197fd462e4c22873fbd70d11b1d2f2c1dc3c` | 0.14.7 | Public domain or MIT-0 |
| `stb_vorbis.c` | [nothings/stb](https://github.com/nothings/stb) | `9600522de53973336fa033a289858a4354cbb04c` | 1.24 | Public domain or MIT |
| `stb_image.h` | [nothings/stb](https://github.com/nothings/stb) | `9600522de53973336fa033a289858a4354cbb04c` | 2.32 | Public domain or MIT |
| `stb_truetype.h` | [nothings/stb](https://github.com/nothings/stb) | `9600522de53973336fa033a289858a4354cbb04c` | 1.28 | Public domain or MIT |
| `../data/font_regular.ttf` | [notofonts/notofonts.github.io](https://github.com/notofonts/notofonts.github.io) (`fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf`) | `cfa91e36c7240b622fa7fcd3a90eeff15e28ae1c` | Noto Sans | SIL OFL 1.1 |
| `../data/font_bold.ttf` | [notofonts/notofonts.github.io](https://github.com/notofonts/notofonts.github.io) (`fonts/NotoSans/hinted/ttf/NotoSans-Bold.ttf`) | `cfa91e36c7240b622fa7fcd3a90eeff15e28ae1c` | Noto Sans | SIL OFL 1.1 |
| `NotoSans-OFL.txt` | [notofonts/latin-greek-cyrillic](https://github.com/notofonts/latin-greek-cyrillic) (`OFL.txt`) | `ffaec0a756877220dbf718ce08b8479a0be82479` | - | - |

The fonts are renamed (`NotoSans-Regular.ttf` → `font_regular.ttf`) so that
the build can embed them as `font_regular_ttf`. They are otherwise unchanged.

## SHA-256

```
7a6cc38b0afdb55cfc9eec991d9169d67beb8fd910be2995121510cdaa99c356  dr_mp3.h
3f010d3142a5b9df80a0bd8b325de54317468b1aff85abb257c86dbfefe9bd2a  dr_flac.h
7c9660d75361ad6175e6217c63a1075c9512445c06bc951197fc479179f76b39  dr_wav.h
b52cf518355f7b2d181f18b97b812f361ffeb26299d935c8cf29e842401d60a6  stb_vorbis.c
deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91  stb_image.h
428cfe93520f2aed5e413a220b0db5bba5b17e2ebe5a64e4a8efa9eeeb67ec5f  stb_truetype.h
cee9892f9f0cc8fe882c9e9537ee6a89621d86ee7ceaf70b02e2b2b1c25c061a  NotoSans-OFL.txt
478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823  ../data/font_regular.ttf
1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913  ../data/font_bold.ttf
```

## Libraries linked into the .nro

These come from the devkit image (not this folder) and are linked statically.
Their licenses are here:

| Library | Version | Where from | License |
|---------|---------|------------|---------|
| FFmpeg (libavformat, libavcodec, libswresample, libavutil) | 7.1 | Built by `ffmpeg/Dockerfile` from ffmpeg.org's release tarball (SHA-256 checked), only the AAC, ALAC and Opus decoders, the MP4, Ogg and ADTS demuxers and the resampler | LGPL 2.1 (`FFmpeg-LGPL-2.1.txt`) |
| libsmb2 | 6.0.0 | Built by `smb/Dockerfile` from its release tarball (SHA-256 checked) | LGPL 2.1 (`libsmb2-LGPL-2.1.txt`, `libsmb2-COPYING.txt`) |
| libcurl | 7.69.1 | devkitPro's `switch-curl` package | curl license (`curl-LICENSE.txt`) |
| zlib | 1.3.1 | devkitPro's `switch-zlib` package | zlib license (`zlib-LICENSE.txt`) |
| OunceVK (Mesa's NVK and Zink: EGL and OpenGL ES), `libEGL.a` and `libouncevk-gl.a` | - | ounce-devkit | MIT (`Mesa-MIT.txt`, `Mesa-license.rst`) |
| libounce (libnx fork, with mimalloc) | - | ounce-devkit | ISC (`libounce-ISC.txt`, `libnx-ISC.txt`); mimalloc MIT (`mimalloc-MIT.txt`) |

Under the LGPL, you may change FFmpeg or libsmb2 and relink KaiPlayer with
your version: the whole source and the scripts that build them are in this
repository.
