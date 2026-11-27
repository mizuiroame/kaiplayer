# Online music (the Internet Archive): plan and status

Branch `feat/streaming` (from main 7645fc9). This file says what was decided, what's done and
what's left, so the work can be picked up again. Not for the release notes.

## Decisions

- **2026-11-27, owner: Jamendo is dropped.** Only the Internet Archive is built. The Jamendo code,
  strings, fixtures and docs from the first commits were removed in later commits (history kept).
  It needs no key or account (its Settings page, added later, has its color only).
- **2026-11-27, owner: show what the Archive is good for**: the Live Music Archive's concerts, 78 rpm
  records, public-domain classical music, and search.
- **2026-11-27, owner: keep the 78 rpm rows.**
- **2026-11-27, owner: the Internet Archive is a source of its own**, switched to as the SD card, the
  network folder and Navidrome are (the pill, X on Home and Library's first page, a tap on the pill,
  **+ → Go to the Internet Archive**), with its own color (amber, changeable as the others'), and not a
  row in Library (it showed inside the card's and Navidrome's libraries). Its Settings page has its
  Color only, as the per-source color convention asks. "Show 50 more" added (up to 500).
- **2026-11-27, owner (after a console test: works well): an On/Off switch**, "Use the Internet
  Archive", On by default, on its Settings page before Color (the other sources have no switch: they
  are on once set up, off when forgotten; the Archive's page is where its settings are). Off: out of
  the cycle and the + menu, the card's library if it was shown, nothing connects to archive.org
  (streamfile.c refuses to open, the network isn't started for it), and its songs say "The Internet
  Archive is turned off in Settings: its songs don't play." (when asked to play, and as each fails).

## What the Internet Archive's terms say (read 2026-11-26)

Terms of Use (archive.org/about/terms.php; the page is now JavaScript-only, read from its 2023 copy,
dated 31 Dec 2014), and the developer portal's "Bots, LLMs, and Automated Access":

- Access is at no cost "for scholarship and research purposes"; "your use of any part of the Archive's
  Collections will be limited to noninfringing or fair use under copyright law." "If a Creative Commons
  or other license has been declared for particular material on the Archive, to the extent you trust
  the declaration and declarer (which is rarely the Internet Archive), you may use the content according
  to the terms and conditions of the applicable license." The Archive doesn't guarantee content is
  non-infringing: the user is responsible.
  So KaiPlayer only searches collections with a clear permission:
  - the **Live Music Archive** (`collection:etree AND mediatype:etree`): bands that allow taping and
    non-commercial trading; some limit what's there, and the Archive enforces it (e.g. stream-only items);
  - the **78 rpm collection** (`collection:78rpm AND mediatype:audio`): the Archive's own transfers of
    old shellac records. Note: the Great 78 Project was sued by record labels in 2023 (reported settled in
    2025; not checked further). What the Archive still serves is its call, and the owner may want to
    drop these two rows if that's a concern; KaiPlayer says "78 rpm collection" as the license line when the item declares none;
  - **public-domain classical** (`licenseurl:*publicdomain* AND subject:classical`, LibriVox left out);
  - **free music**: audio with a declared Creative Commons license (LibriVox audiobooks left out).
  It never offers "all audio" (user uploads of commercial music).
- Not to interfere with the Archive's servers. Automated tools: a descriptive User-Agent (name and
  version: KaiPlayer's already says `KaiPlayer/<version> (+github URL)`), honor 429 (KaiPlayer says
  "busy, try again in a minute" and doesn't retry by itself), cache, no bulk work. KaiPlayer makes one
  request per page opened and keeps the answer while the page is in the stack.
- No key or account. Downloads are allowed by the Archive, but KaiPlayer has none: songs are read as
  they play (the usual read-ahead), nothing is kept on the card.
- Attribution: every song shows its creator, its license (or "Live Music Archive: non-commercial",
  "78 rpm collection"), "Internet Archive" and its item's page `archive.org/details/<id>`, in Now
  Playing's line and in **+ → About this song**.

## Design

Copied from Navidrome where it fits; Navidrome's library is read whole into the catalog, which the
Archive can't be, so it's browsed:

- **A source**: `LIBRARY_ARCHIVE`, always ready (nothing to set up), last in X's cycle: SD card,
  network folder, Navidrome (those set up), Internet Archive. `settings.txt` keeps it
  (`library_source=archive`, `archive_color=`). It has no catalog: switching to it reads nothing,
  and the card's catalog stays loaded underneath (switching back reads it as usual).
- **Its tabs** (decided): **Library** = the Archive's own first page (the six rows), as Library's first
  page (no Back; X switches source there, as on the others' first page); its pages use the Library's
  page stack: `< Back`, B, the `+` button on song rows, X plays / Y shuffles an item, A opens or plays,
  all by touch too. **Home** = what the Archive has and one row, "Browse the Internet Archive", that
  opens Library (A or a tap); X switches source as usual. **Audiobooks** unchanged (books aren't a
  source's). **Playlists, Queue, Now Playing** as always. **+ → Update the library** isn't offered
  under it (and Settings' row says there's nothing to update).
- **Its first page**: Popular live concerts, Search live concerts, Popular 78 rpm records, Search 78
  rpm records, Public-domain classical music, Search free music. An item opens its songs (the item's
  VBR MP3 files, else 128/64 kbps MP3, else Ogg, else FLAC).
- **50 results a page**, and a **Show 50 more** row while the Archive has more (up to 500).
- **Songs are locations**, as Navidrome's `sub:/`: `ia:/<identifier>/<license>/<file, percent-encoded>`;
  covers `ia:/<identifier>/cover/<px>.jpg` (archive.org/services/img). The license code (`by-sa-4.0`,
  `cc0-1.0`, `pd`, `lma`, `78rpm`, `-`) is in the location, so a playlist or the resumed queue still
  shows it after a restart. kfile reads them through `streamfile.c` (HTTP ranges from
  archive.org/download, which redirects to a data node; checked live: 206 answers).
- **Names**: the answers' title, creator and album are kept in memory for the lists and Now Playing
  (`streammeta.c`); after a restart they come from the files' own tags.
- **Playlists** hold these locations as they hold `sub:/` ones (m3u8 lines, absolute; tested).
- **The network** starts the first time the Archive is opened, or a playlist/queue with its songs
  plays; KaiPlayer remembers it was used (`settings.txt`: `online=1`) and then starts it at start.

## Owner questions

None open.

## Status

- [x] Phase 1: research and plan.
- [x] `stream.c` / `streamparse.c` pure modules (locations, licenses, URLs, answers) + host tests with
      made-up fixtures (`tests/fixtures/archive_*.json`), and the fuzzer reads them.
- [x] `streammeta.c` names kept for the session.
- [x] `streamnet.c` / `streamfile.c` (requests on a thread, kfile reads).
- [x] App: Library row and pages, + menu "About this song", Now Playing's license line.
- [x] Strings in the 8 language files; lang check 0/0/0.
- [x] Preview scenes (made-up content).
- [x] Docs: docs/internet-archive.md, README, library, controls, limitations, legal, building.
- [x] The Archive as a source (2026-11-27): pill, X, + menu, its color and Settings page, Home, "more".
- [x] Its On/Off switch (2026-11-27).
- [ ] Console test (owner).
- Left for later: a cover for items without one; browsing the Archive's collections beyond these six
  rows.
