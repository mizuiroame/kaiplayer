# Security

[Back to the README](../README.md)

Under kaiHEN, homebrew runs with broad system access. A bug in an app that
reads untrusted files could therefore affect the whole console. KaiPlayer
reduces that risk in these ways:

- **Small attack surface.** No network access. The app only reads the files
  and folders you browse, and only writes its own files (settings, history,
  catalog, resume) in its folder, and the playlists you save.
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
  Ogg, WAV, DSF, DSDIFF, JPEG and PNG files to every parser, with the
  sanitizers on. Run it before each release.
- **Pinned dependencies.** The libraries in `third_party/` and the fonts in
  `data/` are pinned to upstream commits with SHA-256 sums. See
  [third_party/VERSIONS.md](../third_party/VERSIONS.md), and check them with
  `sh third_party/verify.sh`.

The main remaining risk is in the third-party decoders, which parse files from
the SD card. Only play files from sources you trust.

To report a vulnerability, please open a private security advisory on GitHub
instead of a public issue.
