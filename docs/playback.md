# Playback

[Back to the README](../README.md)

## Supported audio formats

The decoders are built into the `.nro`, so you don't need to install anything
else.

| Format         | Extensions     | Notes                                                 |
|----------------|----------------|-------------------------------------------------------|
| **MP3**        | `.mp3`         | CBR and VBR. The MP3 patents expired in 2017.         |
| **FLAC**       | `.flac`        | Lossless, 8 to 32 bits.                               |
| **Ogg Vorbis** | `.ogg`, `.oga` | Vorbis only. `.ogg` files that contain Opus don't play. |
| **WAV**        | `.wav`         | PCM, IEEE float, A-law and µ-law.                     |
| **DSD**        | `.dsf`, `.dff` | DSD64 to DSD512, converted to PCM. See [DSD](#dsd-dsf-dff). |

- **Sample rates:** any sample rate works (44.1 kHz, 48 kHz, 96 kHz and so on).
  KaiPlayer converts it to the console's 48 kHz.
- **Channels:** mono is played on both channels. For files with more than two
  channels, KaiPlayer plays the front left and front right channels.

**Not supported yet:** Opus, AAC/M4A, ALAC, WMA and DRM-protected files. AAC
and WMA won't be added, because they need paid licenses.

## DSD (.dsf, .dff)

KaiPlayer plays DSD files **by converting them to PCM as they play**. The
console's audio output only takes PCM, so there is no native DSD and no DoP:
what you hear is 48 kHz PCM, like every other format on the console.

| | |
|---|---|
| **Plays** | `.dsf` (DSF) and `.dff` (DSDIFF), uncompressed |
| **Rates** | DSD64, DSD128 and DSD256 (tested on the console); DSD512 also opens. Both the 44.1 kHz family (2.8, 5.6, 11.2 MHz) and the 48 kHz family (3.1, 6.1, 12.3 MHz) |
| **Channels** | Stereo or mono; from multichannel files (up to 6 channels), the front left and right |
| **Tags** | The ID3v2 tag of `.dsf` files (title, artist, album, cover, ReplayGain); `.dff` files only when they carry an `ID3 ` chunk |
| **Doesn't play** | DST-compressed `.dff` (common in SACD rips: convert them to uncompressed DSF or DFF first), SACD `.iso` images, DSD in WavPack (`.wv`) |

**How it's converted.** A low-pass filter removes what's above about 38 kHz
(where DSD keeps its noise) and the 1-bit stream becomes 88.2 kHz PCM
(96 kHz for the 48 kHz family), which then goes to 48 kHz like any other
file. The decoder is KaiPlayer's own (`source/dsd.c`), written from the
published DSF and DSDIFF format descriptions; it uses no third-party code.

**Volume.** A SACD's full level is half of PCM's full scale, so DSD plays
about **6 dB quieter** than a CD-level track. KaiPlayer leaves it that way so
loud passages never clip. With [ReplayGain](settings.md#replaygain) on and
tagged files, the levels usually even out.

## The format label

Now Playing shows the song's format above the progress bar, and the player
bar before the time: **FLAC · 24-bit · 96 kHz**, **MP3 · 44.1 kHz**,
**DSD64 · 2.8 MHz** (bits only for lossless PCM formats).

## Queue, shuffle and repeat

- **Repeat queue** plays the whole queue again after its last song.
- **Repeat song** plays the current song over and over.
- With **Shuffle on**, the Queue tab lists the songs in the shuffled order
  they will play in.
- The queue shows the folder each track comes from.

The buttons for them are in [Controls](controls.md).

## Resume where you left off

Unless you turn it off in [Settings](settings.md#resume-where-you-left-off),
KaiPlayer remembers the queue, the current song and the position. When you
open it again, it starts in Now Playing and keeps playing from that spot (or
waits paused, if you chose **Paused** in Settings).

It saves this in `resume.m3u8` and `resume.txt` in its folder:

- when the song changes,
- when you pause,
- every 30 seconds while playing,
- and when you exit.

## Volume

Use the console's volume buttons to change the volume; KaiPlayer plays at
full level and has no volume of its own.

## Screen and resolution

KaiPlayer draws with the console's GPU, at the screen's own resolution:
1920 × 1080 handheld, and the TV's resolution docked (1440p and 4K TVs get
text and edges drawn at their full sharpness). Going in and out of the dock,
sleep, and turning the screen off while music plays all work. Album art and
background pictures are made at 1080p and scaled up on bigger screens.
