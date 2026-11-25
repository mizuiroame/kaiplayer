# Security

[Back to the README](../README.md) · How to report a problem: [SECURITY.md](../SECURITY.md)

Under kaiHEN, homebrew runs with broad system access. A bug in an app that
reads untrusted files could therefore affect the whole console. KaiPlayer
reduces that risk in these ways:

- **Small attack surface.** Besides the update check below, no network access
  unless you set up a
  [network folder](network-folder.md) or a [Navidrome server](navidrome.md):
  then the console's network is started and KaiPlayer only talks to that
  folder's server (SMB or WebDAV), to list and read its files, or to the
  Navidrome server, to read its library, songs and covers and tell it what
  was played. Once a day (Settings > About > Check once a day turns it off)
  and when you choose **Look for an update**, it asks GitHub, over `https://`
  with its certificate checked, for this repository's latest release.
  Installing one downloads `KaiPlayer.nro` only from this repository's
  release, checks its size and SHA-256 against what GitHub lists for it, and
  only then puts it in place, keeping the old one until the swap is done. From
  brewstore, KaiPlayer only says a new version is out. The app only reads the
  files and folders you browse, and only
  writes its own files (settings, history, catalogs, resume, the network
  folder's and the server's accounts) in its folder, and the playlists you
  save.
- **Only http and https.** Every web request is limited to `http://` and
  `https://` (no `file:`, `ftp:` or other schemes, even through a redirect),
  and an `https://` address can't be redirected to plain `http://`.
  Certificates are checked unless you turn **Certificate check** off for the
  server. Plain `http://` to WebDAV outside your home network gets a warning.
- **What the card keeps of the network folder's password is as good as the
  password.** The settings are on the SD card, not encrypted: anyone who
  copies the card's files has them. For a shared folder (SMB), `network.txt`
  holds the password's NT hash, which is what Windows signs in with: the
  password can't be read back from it, but the hash signs in as that user
  just as the password does (to that share, and to anything else that
  account can open over SMB). For WebDAV
  the password is only encoded, since WebDAV sends the password itself:
  anyone with the file can decode it. With **Remember the password** off,
  nothing of it is written, and it's asked for when the folder is first used.
  A new address forgets the old user and password, so they're never sent to
  another server. Give KaiPlayer
  [a user of its own](network-folder.md#a-user-of-its-own) that can only read
  the folder.
- **Shared folders are signed.** SMB connections require signing, so nobody
  on the network can change what's read. **Allow without signing (old NAS)**
  turns that off for a folder whose server can't sign.
- **The Navidrome server's password isn't kept, but its token is as good as
  a password for the Subsonic API.** `navidrome.txt`, on the SD card and not
  encrypted, holds the user, a random salt and the MD5 of the password with
  it (Subsonic's token), which is what each request carries; the password
  itself is never sent or written. Whoever has the file can use that account
  through the Subsonic API (list, play and download its music, change its
  playlists and stars, and anything else the API lets that user do) until
  the password is changed on the server; it doesn't open the server's web
  page, which asks for the password. Plain `http://` sends the token
  unencrypted. A new address forgets the old sign-in. URLs with the token are
  never written anywhere. Use an account that only plays music.
- **What the server sends is untrusted.** Folder listings (WebDAV's XML, SMB
  entries) are parsed with length checks; names with control characters or
  slashes, `.` and `..`, and listings past 5000 entries are left out; songs
  read over the network go through the same decoders and checks as the
  card's. A Navidrome server's answers (JSON, at most 16 MB each, 64 levels
  deep) are read by a strict parser that the fuzzer runs; ids are checked and
  percent-encoded before they go in a location or a URL, and a library stops
  at 200,000 songs. A file size above 1 TB counts as unknown.
- **Untrusted input is bounded.**
  - Paths and settings use length-checked buffers.
  - Audio headers with unsupported channel counts (0 or more than 8) or sample
    rates (below 1 kHz or above 768 kHz) are rejected.
  - Folder listings and recursive scans have entry, depth and track limits.
- **Images are checked before decoding.** Only JPEG and PNG are decoded, and
  covers over 2048×2048 pixels or 8 MB are refused before any memory is
  allocated for them. The tag readers check every length against the data.
- **Hardening.** The `.nro` is built with `-fstack-protector-strong` and
  `-fwrapv` (signed overflow wraps instead of being undefined). The host
  tests run under AddressSanitizer and UndefinedBehaviorSanitizer.
- **Fuzzing.** `make -C tests fuzz` feeds thousands of corrupted MP3, FLAC,
  Ogg (Vorbis and Opus), WAV, DSF, DSDIFF, M4A (AAC and ALAC), Opus, ADTS AAC,
  JPEG and PNG files, Navidrome answers and network folder account files to
  every parser, with the sanitizers on. Run it before each release.
- **Pinned dependencies.** The libraries in `third_party/` and the fonts in
  `data/` are pinned to upstream commits with SHA-256 sums. See
  [third_party/VERSIONS.md](../third_party/VERSIONS.md), and check them with
  `sh third_party/verify.sh`.

## Known risks

- **libcurl is old.** Every web request (the WebDAV folder, the Navidrome
  server, the update check) goes through libcurl 7.69.1, from devkitPro's
  packages: it's from 2020, and has known vulnerabilities fixed upstream
  since. It's the clearest known risk, and stays until devkitPro updates its
  package. Until then, prefer servers you run yourself, on your home network
  or over `https://` with **Certificate check** on.
- **The third-party decoders** parse files from the SD card and the network.
  Only play files from sources you trust.
