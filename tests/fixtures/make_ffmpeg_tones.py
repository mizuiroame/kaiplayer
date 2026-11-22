"""Makes the AAC, ALAC and Opus fixtures with ffmpeg (in Docker, the
linuxserver/ffmpeg image) from a sine tone and small_cover.jpg:

  tone.m4a        AAC-LC, 1 s, tags, the cover (covr) and ReplayGain in
                  iTunes "----" atoms (added here: ffmpeg doesn't write them)
  tone_alac.m4a   ALAC, 24-bit, 96 kHz, 0.5 s, tags
  tone.opus       Opus, 1 s, tags, R128_TRACK_GAIN and the cover as a
                  METADATA_BLOCK_PICTURE comment
  tone_opus.ogg   Opus in an .ogg file, mono, 1 s, tags
  tone.aac        ADTS AAC, 1 s, no tags

Run from this folder: python make_ffmpeg_tones.py (Docker, no packages)."""
import base64
import os
import struct
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGE = "linuxserver/ffmpeg"


def ffmpeg(*args: str) -> None:
    subprocess.run(["docker", "run", "--rm", "-v", f"{HERE}:/f", "-w", "/f", IMAGE,
                    "-hide_banner", "-loglevel", "error", "-y", *args], check=True,
                   env={**os.environ, "MSYS_NO_PATHCONV": "1"})


def tone(seconds: float, rate: int, freq: int = 440) -> list[str]:
    return ["-f", "lavfi", "-i", f"sine=frequency={freq}:duration={seconds}:sample_rate={rate}"]


def flac_picture(jpeg: bytes) -> bytes:
    """A FLAC PICTURE block: a front cover (type 3), image/jpeg, 64x64."""
    mime = b"image/jpeg"
    return (struct.pack(">II", 3, len(mime)) + mime + struct.pack(">I", 0) +
            struct.pack(">IIIII", 64, 64, 24, 0, len(jpeg)) + jpeg)


def atom(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", 8 + len(body)) + kind + body


def itunes_text(name: str, value: str) -> bytes:
    """An iTunes freeform atom, as foobar2000 and others write ReplayGain."""
    mean = atom(b"mean", b"\0\0\0\0com.apple.iTunes")
    label = atom(b"name", b"\0\0\0\0" + name.encode())
    data = atom(b"data", struct.pack(">II", 1, 0) + value.encode())
    return atom(b"----", mean + label + data)


def add_to_ilst(path: str, extra: bytes) -> None:
    """Appends atoms to moov/udta/meta/ilst. The moov must be at the end of
    the file (ffmpeg's default), so no chunk offsets move."""
    data = bytearray(open(path, "rb").read())
    chain = [b"moov", b"udta", b"meta", b"ilst"]
    starts, pos, end = [], 0, len(data)
    for depth, kind in enumerate(chain):
        while pos < end:
            size, found = struct.unpack(">I4s", data[pos:pos + 8])
            if found == kind:
                break
            pos += size
        else:
            raise SystemExit(f"no {kind!r} in {path}")
        starts.append(pos)
        end = pos + size
        pos += 12 if kind == b"meta" else 8  # meta is a full box: version and flags first.
    if starts[0] + struct.unpack(">I", data[starts[0]:starts[0] + 4])[0] != len(data):
        raise SystemExit("moov is not at the end")
    data[end:end] = extra
    for start in starts:
        size = struct.unpack(">I", data[start:start + 4])[0]
        data[start:start + 4] = struct.pack(">I", size + len(extra))
    open(path, "wb").write(bytes(data))


def main() -> None:
    tags = ["-metadata", "title=Test Title", "-metadata", "artist=Tëst Ártist",
            "-metadata", "album=Fixture Album", "-metadata", "album_artist=Fixture Band",
            "-metadata", "track=3/12", "-metadata", "disc=1/2", "-metadata", "date=2021-05-01",
            "-metadata", "genre=Test"]

    ffmpeg(*tone(1, 44100), "-i", "small_cover.jpg", "-map", "0:a", "-map", "1", "-ac", "2",
           "-c:a", "aac", "-b:a", "128k", "-c:v", "copy", "-disposition:v", "attached_pic",
           *tags, "tone.m4a")
    add_to_ilst(os.path.join(HERE, "tone.m4a"),
                itunes_text("replaygain_track_gain", "-6.50 dB") +
                itunes_text("replaygain_album_gain", "-7.25 dB"))

    ffmpeg(*tone(0.5, 96000), "-ac", "2", "-c:a", "alac", "-sample_fmt", "s32p",
           "-metadata", "title=ALAC Tone", "-metadata", "artist=Tëst Ártist",
           "-metadata", "album=Fixture Album", "-metadata", "track=4", "tone_alac.m4a")

    picture = base64.b64encode(flac_picture(open(os.path.join(HERE, "small_cover.jpg"), "rb").read())).decode()
    ffmpeg(*tone(1, 48000), "-ac", "2", "-c:a", "libopus", "-b:a", "96k", *tags,
           "-metadata", "R128_TRACK_GAIN=-2560", "-metadata", f"METADATA_BLOCK_PICTURE={picture}",
           "tone.opus")

    ffmpeg(*tone(1, 48000, 660), "-ac", "1", "-c:a", "libopus", "-b:a", "48k",
           "-metadata", "title=Opus in Ogg", "-metadata", "artist=Tëst Ártist",
           "-metadata", "album=Fixture Album", "-f", "ogg", "tone_opus.ogg")

    ffmpeg(*tone(1, 44100), "-ac", "2", "-c:a", "aac", "-b:a", "96k", "-f", "adts", "tone.aac")


if __name__ == "__main__":
    main()
