# Building from source

[Back to the README](../README.md)

KaiPlayer is built with [ounce-devkit](https://github.com/ouncebrew/ounce-devkit),
which is devkitPro plus libounce, a libnx for Switch 2, and libsmb2 (for
network folders over SMB) built over it: the `kaiplayer-devkit` image. Docker
is the easiest way to build it.

## With Docker (Windows, Linux, macOS)

1. Build the devkit image. You only need to do this once:

   ```sh
   git -c core.autocrlf=false clone https://github.com/ouncebrew/ounce-devkit
   cd ounce-devkit
   docker build -t ounce-devkit .
   cd ../kaiplayer
   docker build -t kaiplayer-devkit smb   # libsmb2 6.0.0 over ounce-devkit
   ```

   > On Windows you must clone with `core.autocrlf=false`. Otherwise the
   > scripts get CRLF line endings and `install.sh` fails inside the image.

   `smb/Dockerfile` builds libsmb2 6.0.0 (LGPL 2.1) from its release tarball,
   checked by SHA-256, with the Switch 2 environment. curl comes with the
   devkit.

2. Build KaiPlayer.

   On Windows (PowerShell):

   ```powershell
   .\build.ps1           # builds KaiPlayer.nro
   .\build.ps1 test      # host tests (ASan/UBSan + coverage)
   .\build.ps1 fuzz      # fuzzes every file parser ($env:ITER sets the count)
   .\build.ps1 preview   # renders UI screenshots, no console needed
   .\build.ps1 clean
   ```

   On Linux or macOS:

   ```sh
   docker run --rm -v "$PWD:/build/kaiplayer" -w /build/kaiplayer kaiplayer-devkit make
   docker run --rm --security-opt seccomp=unconfined \
     -v "$PWD:/build/kaiplayer" -w /build/kaiplayer/tests kaiplayer-devkit make coverage
   docker run --rm --security-opt seccomp=unconfined \
     -v "$PWD:/build/kaiplayer" -w /build/kaiplayer/tests kaiplayer-devkit make fuzz ITER=3000
   docker run --rm -v "$PWD:/build/kaiplayer" -w /build/kaiplayer/tests/preview kaiplayer-devkit make
   ```

The output is `KaiPlayer.nro`, in the project root.

`preview` compiles the real interface code on the host with a fake player
and writes `tests/preview/build/*.ppm` screenshots of each tab.

The unit tests read network folders through a fake one (the fixtures behind
`dav:/` names), with blocks small enough that every read crosses some: the
decoders, tags and covers must read the same as from the card.

`tests/host_live/` tests a real network folder on a PC: it lists it, reads
songs on their own connection and the shared one, plays them through the
same reader as the console (the samples must be the card's copy's), reads
their tags and covers, and checks that a wrong password, share or address
each say so. Its image has gcc, curl and libsmb2 6.0.0:

```sh
docker build -t kaiplayer-host-live tests/host_live
docker run --rm --security-opt seccomp=unconfined --add-host=host.docker.internal:host-gateway \
  -v "$PWD:/w" -v "/path/to/the/same/music:/music:ro" \
  -e SMB_ADDRESS=smb://host.docker.internal:445/Music -e SMB_USER=... -e SMB_PASS=... -e MUSIC=/music \
  -w /w/tests/host_live kaiplayer-host-live make smb
```

An `http://` address in `SMB_ADDRESS` tests a WebDAV folder the same way.
The test expects the made-up album layout of the owner's test server (8 songs
in "Aurora Fields" and "The Night Drive Band").

The tests and the fuzzer run with ASLR turned off (`setarch -R`), because
GCC 12's AddressSanitizer can hang on recent kernels. In Docker, turning ASLR
off needs `--security-opt seccomp=unconfined`.

## Without Docker

Install [devkitPro pacman](https://devkitpro.org/wiki/devkitPro_pacman), then
run `sh install.sh` inside `ounce-devkit/`. After that, in this folder, run:

```sh
make                 # KaiPlayer.nro
make -C tests run    # tests (needs the system gcc)
```

## Making a release

1. Bump `APP_VERSION` in the `Makefile`.
2. Run `.\build.ps1 clean`, then `.\build.ps1`.
3. Tag the release (`git tag v1.0.0 && git push --tags`).
4. Create the GitHub release and attach `KaiPlayer.nro`.

## Project layout

```
source/
  main.c          main loop and controls
  player.c        audio thread: decode → resample → audout
  decoder.c       common interface for MP3 / FLAC / Vorbis / WAV / DSD
  dsd.c           DSF and DSDIFF: reads the 1-bit stream and filters it to PCM
  resampler.c     sample rate conversion (linear interpolation)
  queue.c         play order, shuffle and repeat
  library.c       finds the music in a folder (recursively), on the card or the network
  browser.c       folder browser
  kfile.c         one reader for every music file: stdio on the card; blocks, read
                  ahead on a thread, for the network folder (blockcache.c: which blocks)
  dav.c           the network folder's addresses, "dav:/" locations, listings, account file
  davnet.c        lists and tests the network folder (WebDAV with net.c, or SMB)
  davfile.c       reads its files for kfile.c: SMB reads (smbnet.c) or HTTP ranges
  net.c           HTTP(S) with curl
  smbnet.c        SMB with libsmb2: listing, reading, connections kept or own
  app_network.c   the Network folder page, its test, and starting the console's network
  tags_*.c        ID3 and Vorbis comment readers (title, artist, album, cover)
  cover.c         decodes and scales album art (JPEG / PNG)
  artwork.c       loads tags and covers on a background thread
  meta*.c         title, artist and album for the lists, read in the background
  gfx.h           drawing: shapes, gradients, images, text masks (layout units)
  gfx_gl.c        ...with the GPU (OpenGL ES 3): the console's backend
  gfx_gl_switch.c EGL on the console's window, at its resolution
  gfx.c           ...with the CPU: the host preview and tests (GPU=0 on the console)
  audioformat.c   the "FLAC · 24-bit · 96 kHz" label
  font.c          anti-aliased text with the embedded fonts
  ui*.c           the tabs, the menu and the keyboard
  ui_nowstyle.c   Now Playing's record and CD (ui_cassette.c: the cassette)
  background.c    the background picture: crop, blur and darken
  plainname.c     plain file names the console can list (playlists)
  catalog*.c      the library: artists, albums and songs from the tags (and its cache)
  covercache.c    album covers for Home and the album rows
  history.c home.c  plays, and the Home tab's rows
  config.c        settings saved on the SD card
  input.c         button auto-repeat
  tracklist.c     lists of song paths (folders, playlists, the queue)
  playlist.c      .m3u8 playlists in <music folder>/Playlists
  resume.c        saves and restores the last queue and position
  keyboard*.c     playlist names: the console's keyboard, and a fallback
  menu.c          the + menu and dialogs
  settings.c      the Settings rows and sleep timer choices
  eq.c            10-band equalizer (peaking filters) and its presets
  theme.c         accent colors, including the one taken from the cover
  screen.c        turning the screen off (backlight)
  touch.c         taps and drags from the touch screen
  ui_hit.c        touch targets recorded as the UI is drawn
  app_*.c         input handling for each tab, the menu, settings, equalizer, touch and resume
smb/            the Dockerfile that adds libsmb2 to the devkit image
data/           Noto Sans Regular and Bold, embedded in the .nro
third_party/    dr_libs, stb_vorbis, stb_image, stb_truetype (public domain)
tests/          host tests, fuzzer, fixtures and the UI preview; host_live/: a real network folder
tools/          kaiupload.py (FTP upload with plain names), gltest (the GPU
                backend checked against the CPU one), gpu-spike (a GPU test .nro)
icon.jpg        256×256 icon for hbmenu
```
