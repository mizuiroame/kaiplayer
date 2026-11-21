# Legal notice and licenses

[Back to the README](../README.md)

## Legal notice

- **Independent project.** KaiPlayer is not affiliated with, endorsed by or
  sponsored by Nintendo Co., Ltd. or its subsidiaries.
- **Trademarks.** "Nintendo", "Nintendo Switch" and "Switch 2" are trademarks
  of their owners. They are used here only to say which hardware this program
  works with. "Direct Stream Digital", "DSD" and "Super Audio CD" are
  trademarks of their owners, used only to name the file formats KaiPlayer
  plays.
- **No Nintendo material.** This repository and its releases contain no
  Nintendo code, SDK, firmware, cryptographic keys, games, or graphics or audio
  assets. KaiPlayer is built only with open-source tools and libraries.
- **No circumvention.** KaiPlayer does not circumvent any technical protection
  measure, and it can't be used to copy or run commercial software. It plays
  the audio files that users put on their own card. It doesn't read SACD
  discs or disc images, and it doesn't decode DST-compressed DSD.
- **User responsibility.** Only play music you have the right to listen to.
  Running homebrew (kaiHEN) can put your console at risk, can affect its
  warranty and can go against the manufacturer's terms of service. You do it at
  your own risk.
- **No warranty.** The software is provided "as is", without warranty of any
  kind. See [LICENSE](../LICENSE).

This notice is not legal advice.

## Licenses

KaiPlayer is released under the [MIT License](../LICENSE), which also contains
the legal notice above.

Copyright (c) 2026 Mizuiro.

Third-party libraries in `third_party/`:

| Library | Used for | License |
|---------|----------|---------|
| [dr_mp3, dr_flac, dr_wav](https://github.com/mackron/dr_libs) | Decoding MP3, FLAC and WAV | Public domain (Unlicense) or MIT-0 |
| [stb_vorbis](https://github.com/nothings/stb) | Decoding Ogg Vorbis | Public domain or MIT |
| [stb_image](https://github.com/nothings/stb) | Decoding album art (JPEG, PNG) | Public domain or MIT |
| [stb_truetype](https://github.com/nothings/stb) | Drawing text | Public domain or MIT |
| [Noto Sans](https://github.com/notofonts/latin-greek-cyrillic) (`data/`) | Interface font | SIL Open Font License 1.1 ([third_party/NotoSans-OFL.txt](../third_party/NotoSans-OFL.txt)) |

The `.nro` also links [libounce](https://github.com/ouncebrew/libounce), which
uses the ISC license (its bundled mimalloc uses MIT).
