#!/usr/bin/env python3
"""Upload music to a Switch 2 over FTP with names the console can list.

On Switch 2, files and folders whose names have non-ASCII characters
(accents such as á or ñ, or Japanese) are saved but left out of directory
listings. This script uploads a folder over FTP and gives each file and
folder an ASCII name on the console only: your local files are never
changed. KaiPlayer still shows the real titles from the tags.

    python kaiupload.py 192.168.1.50 "G:/Music/Red Hot Chili Peppers" /music
    python kaiupload.py 192.168.1.50 "G:/Music/Album" /music --dry-run

Run it from PowerShell or cmd: Git Bash turns "/music" into a Windows path.

Names: accents are dropped (Canción -> Cancion, Ñandú -> Nandu); other
characters such as Japanese become "_" (04 夜の港.mp3 -> 04 _.mp3).
Files already on the console with the same size are skipped, so an
interrupted upload can be run again. Needs only Python 3.8+.
"""
from __future__ import annotations

import argparse
import ftplib
import posixpath
import re
import sys
import unicodedata
from pathlib import Path

# Characters FTP and the console's file system handle in names.
SAFE = re.compile(r"[^A-Za-z0-9 _.,()'&!+\-\[\]]+")


# Typographic punctuation with a plain ASCII look-alike.
PUNCTUATION = str.maketrans({
    "‘": "'", "’": "'", "‚": "'", "′": "'",
    "“": "'", "”": "'", "„": "'", "«": "'", "»": "'",
    "‐": "-", "‑": "-", "‒": "-", "–": "-", "—": "-",
    "…": "...", "¿": "", "¡": "",
})


def ascii_name(name: str) -> str:
    """An ASCII version of a file or folder name."""
    decomposed = unicodedata.normalize("NFKD", name.translate(PUNCTUATION))
    stripped = "".join(c for c in decomposed if not unicodedata.combining(c))
    clean = SAFE.sub("_", stripped)
    clean = re.sub(r"_+", "_", clean).strip(" ")
    clean = clean.rstrip(".")  # A leading dot may be all that's left of the stem.
    stem, dot, ext = clean.rpartition(".")
    if dot and not stem.strip("_ ."):
        clean = "track" + dot + ext  # Nothing readable was left.
    return clean or "untitled"


def unique_names(names: list[str], dirs: set[str] | None = None) -> dict[str, str]:
    """Maps each local name to an ASCII name, unique among its siblings."""
    taken: set[str] = set()
    result = {}
    for name in sorted(names):
        candidate = ascii_name(name)
        stem, dot, ext = candidate.rpartition(".")
        if not dot or (dirs and name in dirs):  # Folders have no extension.
            stem, dot, ext = candidate, "", ""
        n = 2
        while candidate.lower() in taken:
            candidate = f"{stem} ({n}){dot}{ext}"
            n += 1
        taken.add(candidate.lower())
        result[name] = candidate
    return result


def remote_size(ftp: ftplib.FTP, path: str) -> int | None:
    try:
        return ftp.size(path)
    except ftplib.all_errors:
        return None


def ensure_dir(ftp: ftplib.FTP | None, path: str) -> None:
    """Creates path and any missing parents."""
    if ftp is None:
        return
    current = ""
    for part in path.strip("/").split("/"):
        current += "/" + part
        try:
            ftp.mkd(current)
        except ftplib.error_perm:
            pass  # Already there.


def upload_tree(ftp: ftplib.FTP | None, local: Path, remote: str) -> tuple[int, int]:
    """Uploads local into remote. Returns (uploaded, skipped)."""
    uploaded = skipped = 0
    ensure_dir(ftp, remote)
    entries = list(local.iterdir())
    names = unique_names([e.name for e in entries], {e.name for e in entries if e.is_dir()})
    for entry in sorted(entries, key=lambda e: e.name):
        target = posixpath.join(remote, names[entry.name])
        if entry.is_dir():
            u, s = upload_tree(ftp, entry, target)
            uploaded += u
            skipped += s
            continue
        size = entry.stat().st_size
        renamed = "" if names[entry.name] == entry.name else f"  (from {entry.name})"
        if ftp is not None and remote_size(ftp, target) == size:
            skipped += 1
            continue
        print(f"{target}{renamed}")
        if ftp is not None:
            with entry.open("rb") as f:
                ftp.storbinary(f"STOR {target}", f)
        uploaded += 1
    return uploaded, skipped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("host", help="the console's IP address, as OunceShell's FTP screen shows it")
    parser.add_argument("local", type=Path, help="folder on this computer to upload")
    parser.add_argument("remote", help="folder on the SD card to upload into, e.g. /music")
    parser.add_argument("--port", type=int, default=5000, help="FTP port (default 5000)")
    parser.add_argument("--user", default="anonymous")
    parser.add_argument("--password", default="")
    parser.add_argument("--dry-run", action="store_true", help="only print the names it would use")
    args = parser.parse_args()
    # Windows consoles may not have the characters of the original names.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")

    if not args.local.is_dir():
        print(f"Not a folder: {args.local}", file=sys.stderr)
        return 1
    remote = posixpath.join(args.remote, unique_names([args.local.name])[args.local.name])

    if args.dry_run:
        uploaded, _ = upload_tree(None, args.local, remote)
        print(f"{uploaded} files would be uploaded.")
        return 0

    try:
        with ftplib.FTP() as ftp:
            ftp.connect(args.host, args.port, timeout=30)
            ftp.login(args.user, args.password)
            ftp.encoding = "utf-8"
            ftp.voidcmd("TYPE I")  # Some servers only answer SIZE in binary mode.
            ensure_dir(ftp, args.remote)
            uploaded, skipped = upload_tree(ftp, args.local, remote)
    except (OSError, ftplib.Error) as e:
        print(f"FTP error: {e}", file=sys.stderr)
        return 1
    print(f"Done: {uploaded} uploaded, {skipped} already there.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
