# Known limitations

[Back to the README](../README.md)

- Rewinding in a very long MP3 can take a moment, because dr_mp3 decodes
  again from the start of the file.
- Pause and track changes have about 140 ms of latency, the size of the audio
  buffers.
- A play list holds up to 8192 tracks, from up to 8 levels of subfolders.
- Files and folders with non-ASCII names (accented letters or Japanese) are
  left out of directory listings on Switch 2, in every app. Opening them by
  path works (FTP `SIZE` finds them), so the console's directory read seems
  to stop at such entries. KaiPlayer can't list what the console doesn't
  return; see [File names and tags](file-names.md) for the workaround.
- WMA isn't supported, nor are WavPack, APE and DRM-protected files. See
  [Playback](playback.md).
- A network folder is read while it plays: there is a short gap between its
  songs (the next one's first piece is fetched when it starts), and nothing
  is kept on the card for playing without the network. See
  [Network folder](network-folder.md). The same goes for a
  [Navidrome server](navidrome.md).
- A Navidrome (Subsonic) server: its own playlists aren't shown, Folders has
  nothing for it, songs in formats KaiPlayer doesn't play (WMA, WavPack...)
  are left out rather than converted by the server, and a server that doesn't
  take token sign-in (some with LDAP) can't be used. Its books from a library
  named `Audiobooks` also show among the music (those of a genre named so
  don't). See [Audiobooks](audiobooks.md).
- The Internet Archive: up to 500 items a list (50 at a time), its songs read
  as they play (nothing kept on the card), and only the Live Music Archive,
  the 78 rpm collection and audio with a Creative Commons or public-domain
  license are searched. It's browsed, not read into Artists, Albums and Songs,
  so Home has no cards for it. See [Internet Archive](internet-archive.md).
- DSD is converted to PCM (no native DSD and no DoP), and DST-compressed
  `.dff`, SACD `.iso` images and DSD in WavPack don't play. See
  [DSD](playback.md#dsd-dsf-dff).
