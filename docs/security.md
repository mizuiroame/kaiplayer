# Security

[Back to the README](../README.md)

Under kaiHEN, homebrew runs with broad system access. A bug in an app that
reads untrusted files could therefore affect the whole console. KaiPlayer
reduces that risk in these ways:

- **Small attack surface.** No network access unless you set up a
  [network folder](network-folder.md) or a [Navidrome server](navidrome.md):
  then the console's network is started and KaiPlayer only talks to that
  folder's server (SMB or WebDAV), to list and read its files, or to the
  Navidrome server, to read its library, songs and covers and tell it what
  was played. The app only reads the files and folders you browse, and only
  writes its own files (settings, history, catalogs, resume, the network
  folder's and the server's accounts) in its folder, and the playlists you
  save.
- **The network folder's password** is kept as typed in `network.txt` in
  KaiPlayer's folder, because it's needed to sign in each time. A new address
  forgets the old user and password, so they're never sent to another server.
  Use an account that can only read the folder. Plain `http://` WebDAV sends
  it unencrypted.
- **The Navidrome server's password isn't kept.** `navidrome.txt` holds a
  random salt and the MD5 of the password with it (Subsonic's token), which is
  what each request carries; the password is never sent. The token still lets
  whoever has the file use that account through the Subsonic API, and plain
  `http://` sends it unencrypted. A new address forgets the old sign-in. URLs
  with the token are never written anywhere.
- **What the server sends is untrusted.** Folder listings (WebDAV's XML, SMB
  entries) are parsed with length checks; names with control characters and
  listings past 5000 entries are left out; songs read over the network go
  through the same decoders and checks as the card's. A Navidrome server's
  answers (JSON, at most 16 MB each, 64 levels deep) are read by a strict
  parser that the fuzzer runs; ids are percent-encoded before they go in a
  location or a URL, and a library stops at 200,000 songs.
- **Untrusted input is bounded.**
  - Paths and settings use length-checked buffers.
  - Audio headers with unsupported channel counts (0 or more than 8) or sample
    rates (below 1 kHz or above 768 kHz) are rejected.
  - Folder listings and recursive scans have entry, depth and track limits.
- **Images are checked before decoding.** Only JPEG and PNG are decoded, and
  images over 4096×4096 pixels or 8 MB are refused before any memory is
  allocated for them. The tag readers check every length against the data.
- **Hardening.** The `.nro` is built with `-fstack-protector-strong` and
  `-fwrapv` (signed overflow wraps instead of being undefined). The host
  tests run under AddressSanitizer and UndefinedBehaviorSanitizer.
- **Fuzzing.** `make -C tests fuzz` feeds thousands of corrupted MP3, FLAC,
  Ogg (Vorbis and Opus), WAV, DSF, DSDIFF, M4A (AAC and ALAC), Opus, ADTS AAC,
  JPEG and PNG files to every parser, with the
  sanitizers on. Run it before each release.
- **Pinned dependencies.** The libraries in `third_party/` and the fonts in
  `data/` are pinned to upstream commits with SHA-256 sums. See
  [third_party/VERSIONS.md](../third_party/VERSIONS.md), and check them with
  `sh third_party/verify.sh`.

The main remaining risk is in the third-party decoders, which parse files from
the SD card. Only play files from sources you trust.

To report a vulnerability, please open a private security advisory on GitHub
instead of a public issue.
