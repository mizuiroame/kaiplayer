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
- Opus, AAC, ALAC and WMA aren't supported. AAC and WMA won't be added,
  because they need paid licenses. See [Playback](playback.md).
- DSD is converted to PCM (no native DSD and no DoP), and DST-compressed
  `.dff`, SACD `.iso` images and DSD in WavPack don't play. See
  [DSD](playback.md#dsd-dsf-dff).
