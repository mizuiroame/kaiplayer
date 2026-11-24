# Playlists

[Back to the README](../README.md)

Playlists are saved as standard `.m3u8` files in the **Playlists** folder
inside your music folder (for example `sdmc:/Music/Playlists/`), so all your
music and playlists live in one folder. Without a music folder chosen, they go
in KaiPlayer's own folder (`config/Playlists`, it says where), and move into the
music folder's when you choose one; a name already there gets " (2)".

- Paths are relative to that folder (for example `../Album/01 Song.flac`).
- Other music players can read them, and you can also copy `.m3u` or `.m3u8`
  playlists made on your PC into that folder.
- Playlists from older versions (`sdmc:/playlists/`) are copied there the
  first time; the old files stay where they were.

## Using them

- **Create one:** in the Playlists tab, press **+** and choose
  **New playlist**. Or, in any tab, choose **Add … to playlist** and then
  **New playlist...**.
- **Add songs:** tap the add-to-playlist button on a song or folder in
  Library (an artist, album or folder adds all its songs) or on a song in
  Queue, or **Add to playlist** in Now Playing. Then pick the playlist, or
  **New playlist...**. With buttons, select the row and press **→** (in Now
  Playing, press **B**). Songs already in the playlist aren't added twice.
- **Save the queue:** in Queue, press **+** and choose **Save queue as
  playlist**.
- **Play:** in the Playlists tab, **X** plays a playlist, **Y** shuffles it,
  and **A** opens it to play from a song.
- **Reorder:** open a playlist, select a song, press **+** and choose **Move
  song**. Move it with ↑ ↓ and press **A** to place it.
- **Remove a song:** inside a playlist, each song's row button (a list
  with a minus) removes it from the playlist after you confirm; the file
  stays on the card. It is also in the **+** menu.
- **Rename or delete a playlist:** with **+** in the Playlists tab. Deleting
  a playlist doesn't delete any songs.

If a playlist names a song that is no longer on the card, KaiPlayer skips it
when playing.

## Playlist names

Playlist names are typed on the console's own keyboard and can have **any
characters**: accents (`Español`), Japanese (`松原みき`) and so on, up to 48.

- Since the console hides files whose names have such characters (see
  [File names and tags](file-names.md)), the file gets a plain version of
  the name (`Espanol.m3u8`, or `Playlist.m3u8` when nothing readable is
  left).
- The real name is kept inside it, in a `#PLAYLIST:` line that other players
  ignore.
- Names can't start with a dot or contain `/` or `\`.
- If a name isn't accepted, the keyboard opens again with the reason.
