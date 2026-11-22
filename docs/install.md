# Installing KaiPlayer

[Back to the README](../README.md)

You don't need to build anything. Everything is copied over Wi-Fi with
OunceShell's FTP server, so the microSD card stays in the console.

> **Don't take the microSD card out while kaiHEN is running.** Removing it
> means restarting the console, and kaiHEN has to be started again after
> that.

## Requirements

- A Switch 2 with **kaiHEN** running. Launch a game while holding **R** to open
  the homebrew menu (hbmenu).
- **OunceShell**, which comes with kaiHEN.
- A PC on the same Wi-Fi network as the console, with an FTP client such as
  [FileZilla](https://filezilla-project.org/).

## 1. Download

Go to the repository's
**[Releases](https://github.com/mizuiroame/kaiplayer/releases)** page and
download `KaiPlayer.nro` from the latest release.

## 2. Copy the .nro to the console

1. In hbmenu, open **OunceShell** and start its FTP server. OunceShell shows
   the console's IP address and port (for example `192.168.0.51`, port
   `1337`).
2. In FileZilla, enter that address as **Host** and that number as **Port**,
   leave **Username** and **Password** empty, and click **Quickconnect**.
3. In the remote pane (the console), open the `switch` folder (create it if
   it doesn't exist) and make a `kaiplayer` folder inside it.
4. Drag `KaiPlayer.nro` from your PC into `kaiplayer`. Its path on the card
   is then `sdmc:/switch/kaiplayer/KaiPlayer.nro`. (Straight in `switch`
   works too.)

To update KaiPlayer later, upload the new `KaiPlayer.nro` the same way and
let it replace the old one.

## 3. Copy your music

1. With the same FTP connection, create a folder for your music, for example
   `Music`.
2. Drag your album folders into it. Subfolders (artist, album and so on) are
   fine.

> **Use plain letters (A–Z, no accents) in file and folder names.** On
> Switch 2, a file or folder whose name has any character outside basic
> ASCII (accents such as `á é ñ ü`, typographic quotes such as `’`, or
> Japanese) is saved, but the console's file system doesn't show it: no app
> can list it, KaiPlayer included. The easiest way around it is
> [KaiDrop](https://github.com/mizuiroame/kaidrop) (work in progress), which
> uploads your music over FTP with plain names on the console and leaves your
> files untouched. Or rename the files before uploading, for example
> `06 Canción.mp3` → `06 Cancion.mp3`. KaiPlayer still shows the song's real
> title, artist and album, with accents or in Japanese, from its tags. See
> [File names and tags](file-names.md).

## 4. Play it

1. Open hbmenu (launch a game while holding **R**) and select **KaiPlayer**.
2. The first time, KaiPlayer asks where your music is: open your music
   folder (for example `Music`) with **A** and press **X** to use it.
   KaiPlayer only shows that folder and the ones inside it. You can change
   it later in **+ → Settings → Library → Music folder**. Music can also
   come from a [network folder](network-folder.md) or a
   [Navidrome server](navidrome.md).
3. KaiPlayer reads your songs' tags in the background and builds your
   library. Open **Library → Artists** or **Albums**, pick one and press
   **X** to play it, or **A** on a song to start from there.

KaiPlayer remembers the music folder and the shuffle and repeat modes in
`settings.txt` in its folder (see
[below](#where-kaiplayer-keeps-its-files)).

## Where KaiPlayer keeps its files

Settings, play history, the library catalog and where you left off are kept
in a `config` folder next to `KaiPlayer.nro`, in its own folder:

| Where `KaiPlayer.nro` is | Its files |
|--------------------------|-----------|
| `sdmc:/switch/KaiPlayer.nro` | `sdmc:/switch/kaiplayer/config/` |
| `sdmc:/switch/kaiplayer/KaiPlayer.nro` | `sdmc:/switch/kaiplayer/config/` |
| `sdmc:/brewstore/apps/kaiplayer/KaiPlayer.nro` | `sdmc:/brewstore/apps/kaiplayer/config/` |

Before 1.0.0-rc2 they were in `sdmc:/config/kaiplayer`. The first time a newer
KaiPlayer opens, it copies them over (and leaves that folder as it is, so you
can delete it once everything is there). Your music, playlists and
backgrounds stay where they are.
