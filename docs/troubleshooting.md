# Troubleshooting

[Back to the README](../README.md)

| Problem | Solution |
|---------|----------|
| "No music in this folder" | Check that the files have one of the extensions in the [formats table](playback.md#supported-audio-formats). |
| A file or folder doesn't appear | If its name has accents (á, ñ…), Japanese or other non-ASCII characters, rename it with plain letters. See [File names and tags](file-names.md). Otherwise check that it has a supported extension. |
| "Some items in this folder could not be read" | The console reported an error while listing the folder, so some files may be missing from the list. This can happen with non-ASCII file names. |
| "Could not open: file" | The file is damaged, or it isn't in the format its extension says. KaiPlayer skips it. |
| No cover | The file has no embedded cover and its folder has no `cover.jpg` (or similar), or the image is larger than 4096×4096 or 8 MB. See [Song details and album art](library.md#song-details-and-album-art). |
| `?` in titles | The character isn't in KaiPlayer's font or in the console's fonts. |
| An MP3's length shows as `--:--` | That MP3 has no Xing/Info header. It plays normally, only without a length. |
| The volume bar is gone | KaiPlayer has no volume of its own. Use the console's volume buttons. |
| A playlist name isn't accepted | Use up to 48 characters, without `/` or `\`, not starting with a dot, and a name no other playlist has (capitals don't count: `Mix` and `mix` are the same). |
| A playlist made on the PC doesn't appear | It must be in the `Playlists` folder of your music folder, end in `.m3u` or `.m3u8`, and have a plain file name (no accents): the console hides the others. To show a name with accents, add a line `#PLAYLIST:Name` at the top. |
| Taps land in the wrong place | Please open an issue with your console model and where you tapped. The buttons always work. |
| "Audio unavailable" | The console's audio service could not start. Restart and open KaiPlayer again. If it keeps happening, open an issue. |
