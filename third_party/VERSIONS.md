# Vendored third-party code

These files are copied unmodified from the upstream commits below. Run
`sh third_party/verify.sh` to check them against the SHA-256 sums.

To update a library, download the files from a newer commit, then update the
commit, version and SHA-256 in this file and in `verify.sh`. Review the
upstream changes before you update: these decoders parse untrusted files.

| File | Upstream | Commit | Version |
|------|----------|--------|---------|
| `dr_mp3.h` | [mackron/dr_libs](https://github.com/mackron/dr_libs) | `f12e197fd462e4c22873fbd70d11b1d2f2c1dc3c` | 0.7.5 |
| `dr_flac.h` | [mackron/dr_libs](https://github.com/mackron/dr_libs) | `f12e197fd462e4c22873fbd70d11b1d2f2c1dc3c` | 0.13.5 |
| `dr_wav.h` | [mackron/dr_libs](https://github.com/mackron/dr_libs) | `f12e197fd462e4c22873fbd70d11b1d2f2c1dc3c` | 0.14.7 |
| `stb_vorbis.c` | [nothings/stb](https://github.com/nothings/stb) | `9600522de53973336fa033a289858a4354cbb04c` | 1.24 |
| `font8x8_basic.h` | [dhepper/font8x8](https://github.com/dhepper/font8x8) | `8e279d2d864e79128e96188a6b9526cfa3fbfef9` | - |
| `font8x8_ext_latin.h` | [dhepper/font8x8](https://github.com/dhepper/font8x8) | `8e279d2d864e79128e96188a6b9526cfa3fbfef9` | - |

## SHA-256

```
7a6cc38b0afdb55cfc9eec991d9169d67beb8fd910be2995121510cdaa99c356  dr_mp3.h
3f010d3142a5b9df80a0bd8b325de54317468b1aff85abb257c86dbfefe9bd2a  dr_flac.h
7c9660d75361ad6175e6217c63a1075c9512445c06bc951197fc479179f76b39  dr_wav.h
b52cf518355f7b2d181f18b97b812f361ffeb26299d935c8cf29e842401d60a6  stb_vorbis.c
49d8df366296b203ca3211bc0672cf2a762135bf12710735b6292756b19dffd5  font8x8_basic.h
e0582e9a6b3f85dbb983c72d95c9a50c3547ad5e895d16ef167e20777d053f1b  font8x8_ext_latin.h
```
