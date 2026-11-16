"""Makes tone.dsf and tone.dff: 0.2 s of DSD64, 440 Hz on the left and 880 Hz
on the right at half scale (SACD's 0 dB), from a second-order sigma-delta
modulator, with an ID3v2 tag. Run: python make_dsd.py (no packages needed)."""
import math
import struct

RATE = 2822400
SECONDS = 0.2
BLOCK = 4096


def modulate(freq: float) -> list[int]:
    """1-bit samples (0 or 1) for a half-scale sine."""
    bits, i1, i2, y = [], 0.0, 0.0, -1.0
    for n in range(int(RATE * SECONDS)):
        x = 0.5 * math.sin(2 * math.pi * freq * n / RATE)
        i1 += x - y
        i2 += i1 - y
        y = 1.0 if i2 >= 0 else -1.0
        bits.append(1 if y > 0 else 0)
    return bits


def pack(bits: list[int], lsb_first: bool) -> bytes:
    out = bytearray()
    for i in range(0, len(bits) - len(bits) % 8, 8):
        b = 0
        for j, bit in enumerate(bits[i:i + 8]):
            b |= bit << (j if lsb_first else 7 - j)
        out.append(b)
    return bytes(out)


def id3() -> bytes:
    def frame(fid: str, text: str) -> bytes:
        body = b"\x01" + text.encode("utf-16")  # UTF-16 with a BOM.
        return fid.encode() + struct.pack(">I", len(body)) + b"\x00\x00" + body
    frames = frame("TIT2", "DSD Tone") + frame("TPE1", "Tëst Ártist") + frame("TALB", "Fixture Album")
    size = len(frames)
    syncsafe = bytes([(size >> 21) & 0x7F, (size >> 14) & 0x7F, (size >> 7) & 0x7F, size & 0x7F])
    return b"ID3\x03\x00\x00" + syncsafe + frames


def dsf(left: list[int], right: list[int]) -> bytes:
    channels = [pack(left, True), pack(right, True)]
    per_channel = len(channels[0])
    blocks = -(-per_channel // BLOCK)
    data = bytearray()
    for b in range(blocks):
        for ch in channels:
            data += ch[b * BLOCK:(b + 1) * BLOCK].ljust(BLOCK, b"\x00")
    tag = id3()
    data_chunk = b"data" + struct.pack("<Q", 12 + len(data)) + data
    fmt = b"fmt " + struct.pack("<QIIIIIIQII", 52, 1, 0, 2, 2, RATE, 1, len(left), BLOCK, 0)
    total = 28 + len(fmt) + len(data_chunk) + len(tag)
    head = b"DSD " + struct.pack("<QQQ", 28, total, 28 + len(fmt) + len(data_chunk))
    return head + fmt + data_chunk + tag


def chunk(cid: bytes, body: bytes) -> bytes:
    return cid + struct.pack(">Q", len(body)) + body + (b"\x00" if len(body) % 2 else b"")


def dff(left: list[int], right: list[int]) -> bytes:
    l, r = pack(left, False), pack(right, False)
    data = bytes(b for pair in zip(l, r) for b in pair)
    prop = b"SND " + chunk(b"FS  ", struct.pack(">I", RATE)) + \
        chunk(b"CHNL", struct.pack(">H", 2) + b"SLFTSRGT") + \
        chunk(b"CMPR", b"DSD " + b"\x0enot compressed\x00")
    body = b"DSD " + chunk(b"FVER", struct.pack(">I", 0x01050000)) + chunk(b"PROP", prop) + \
        chunk(b"DSD ", data) + chunk(b"ID3 ", id3())
    return b"FRM8" + struct.pack(">Q", len(body)) + body


if __name__ == "__main__":
    left, right = modulate(440.0), modulate(880.0)
    with open("tone.dsf", "wb") as f:
        f.write(dsf(left, right))
    with open("tone.dff", "wb") as f:
        f.write(dff(left, right))
