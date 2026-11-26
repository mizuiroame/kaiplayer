# Online music (Jamendo and Internet Archive): plan and status

Branch `feat/streaming` (from main 7645fc9). This file says what was decided, what's done and
what's left, so the work can be picked up again. Not for the release notes.

## What the terms say (read 2026-11-26)

### Jamendo: API Terms of Use (devportal.jamendo.com/api_terms_of_use, last modified 2013-09-17)

- **Non-commercial only, free.** 3.3: "The API may be used freely for non-commercial uses." Anything with
  revenue (ads, affiliate programs) needs a quote from licensing@jamendo.com. KaiPlayer is free and has
  no ads: fine.
- **The client_id is personal.** 2: "Any credentials giving access to the API, included but not limited
  to login, password, API key, arising from the registration or provided by JAMENDO is strictly
  personal, and shouldn't, under any circumstances, be shared with or disclosed to third parties."
  One account per person, one unique Client ID per application; Jamendo may delete IDs that break this.
  The authentication page also calls it "a private 'client_id'". **So shipping the owner's client_id
  in a public binary or repo is not allowed by the terms as written** (anyone could read it out of the
  .nro). Decision: nothing is committed; a build can embed `jamendo_client_id.txt` for the owner's own
  test builds; release builds must not, unless Jamendo agrees in writing. Each person types their own
  client ID in Settings (free at devportal.jamendo.com). Owner question below.
- **Attribution.** 4.1: the application must credit the Jamendo members as the creators, credit
  JAMENDO as the provider, and give "a direct backlink from each Content in the Application to the
  relevant Content's page on the JAMENDO Platform". 3.1: each track's Creative Commons license is
  shown on its page. KaiPlayer shows, for every Jamendo song: the artist (Now Playing and lists), the
  license ("CC BY-NC-SA 3.0"), "Jamendo", and the song's page as text (`jamendo.com/track/<id>`, no
  browser on the console), in Now Playing and in the + menu's "About this song".
- **Logos.** 4.1-4.2: only Jamendo's own buttons/logos, unmodified; no Jamendo marks in the app's
  name, logo or colors; nothing suggesting it's official. KaiPlayer uses the plain word "Jamendo",
  no logo, and the docs say it isn't affiliated.
- **No caching or offline access.** 3.3: "Applications must not be specifically designed to cache the
  content nor offering an offline access to the content." Caching only as needed to run.
  **So no download to the SD card**, for Jamendo. Songs are read as they play (the usual read-ahead).
- **Rate limits.** 3.3: Jamendo may limit the number and frequency of requests; don't get around them.
  The authentication page: above 500,000 hits Jamendo may set a temporary limit. No published per-day
  number. KaiPlayer asks only when a page is opened (no polling) and keeps each page's answer while it's
  open; one request to find a song's audio address when it starts.
- Don't replicate "the overall experience of JAMENDO services" (3.3): a few browse/search pages are not.
- Jamendo may change the terms at any time (8); on termination, stop using the API (7).
- The documented test client_id (709fa152) answers "suspended" (code 11): it can't be used.

### Internet Archive: Terms of Use (archive.org/about/terms.php; the page is now JavaScript-only,
read from its 2023 copy, dated 31 Dec 2014), and the developer portal's "Bots, LLMs, and Automated Access"

- Access is at no cost "for scholarship and research purposes"; "your use of any part of the Archive's
  Collections will be limited to noninfringing or fair use under copyright law." "If a Creative Commons
  or other license has been declared for particular material on the Archive, to the extent you trust
  the declaration and declarer (which is rarely the Internet Archive), you may use the content according
  to the terms and conditions of the applicable license." The Archive doesn't guarantee content is
  non-infringing: the user is responsible.
  So KaiPlayer only searches collections with a clear permission: the **Live Music Archive** (`etree`:
  bands that allow taping and non-commercial trading; some limit what's there, the Archive enforces it)
  and audio with a **declared Creative Commons or public-domain license** (`licenseurl`), including the
  netlabels collection. It never offers "all audio" (user uploads of commercial music).
- Not to interfere with the Archive's servers. Automated tools: a descriptive User-Agent (name and
  version: KaiPlayer's already says `KaiPlayer/<version> (+github URL)`), honor 429 and Retry-After,
  cache, add delays for bulk work. KaiPlayer makes one request per page opened, no bulk work.
- No key needed. Downloads are allowed by the Archive, but v1 has no downloads for either service
  (the same code for both, and Jamendo forbids it).
- Attribution: shown the same way (creator, license if declared, "Internet Archive", the item's page
  `archive.org/details/<id>`). Live Music Archive items say "Live Music Archive · non-commercial".

## Design (decided)

Copied from Navidrome where it fits; Navidrome's library is read whole into the catalog, which these
services can't be (millions of songs), so they're browsed instead:

- **Where**: two more rows on the Library tab's first page, after Folders: **Jamendo** and **Internet
  Archive**, whatever library source is shown (they don't change it, and Home and the catalog stay the
  library's). Their pages use the Library's page stack: `< Back`, B, the rows, the `+` button on song
  rows, X plays / Y shuffles the page, A opens or plays; all by touch too.
- **Jamendo pages**: Popular songs, New albums, Genres (a fixed list of Jamendo tags), Search songs,
  Search albums, Search artists. An artist opens their albums, an album its songs.
  Without a client ID, the page says how to get one (and A opens Settings → Online music).
- **Internet Archive pages**: Popular live concerts, Search live concerts (etree), Popular netlabel
  music, Search free music (Creative Commons or public domain audio). An item opens its songs (the
  item's VBR MP3 files, else MP3, else Ogg, else FLAC).
- **50 results a page** (no "more" yet).
- **Settings → Online music** (a new page on the main one): Jamendo client ID, How to get a client ID,
  Test Jamendo, Test the Internet Archive. The client ID is kept in `online.txt` in KaiPlayer's folder.
- **Songs are locations**, as Navidrome's `sub:/`: `jam:/<cover>/<license>/<track id>.mp3` and
  `ia:/<identifier>/<license>/<file, percent-encoded>`. The license code (`by-nc-sa-3.0`, `cc0-1.0`,
  `pd`, `lma`, `-`) is in the location, so a playlist or the resumed queue still shows it after a
  restart; the cover key (Jamendo album `123`, artist `a123`, or `-`) gives the cover. kfile reads
  them through `streamfile.c` (HTTP ranges, as `subfile.c`): Jamendo's audio address comes from the API
  (`tracks?id=`), kept for the last few songs; the Archive's is `archive.org/download/<id>/<file>`.
- **Names**: the results' title, artist and album are kept in memory for the lists and Now Playing
  (`streammeta.c`); after a restart they come from the files' own tags (Jamendo's and the Archive's
  MP3s have them).
- **Playlists** hold these locations as they hold `sub:/` ones (m3u8 lines, absolute): they play when
  the network is up. Checked in playlist.c: "scheme:/" paths are kept absolute.
- **The network** starts the first time online music is opened, or a playlist/queue with its songs
  plays; KaiPlayer remembers it was used (`settings.txt`: `online=1`) and then starts it at start.
- **Downloads**: not offered (Jamendo's 3.3).

## Owner questions (none blocking)

1. Jamendo's client_id is "strictly personal" and must not be disclosed: should the release ship
   without one (each person types theirs; what's built now), or will you ask Jamendo
   (licensing@jamendo.com / devportal) for written permission to embed KaiPlayer's in the public .nro?
2. Settings gets a new main-page row, "Online music": KaiVideo's and KaiRadio's Settings don't have it
   (they are said to have the same pages). OK, or should it live under Library?

## Status

- [x] Phase 1: research and plan (this file).
- [ ] `stream.c` pure module (locations, licenses, URLs, JSON answers) + tests and fixtures.
- [ ] `streammeta.c` names kept for the session.
- [ ] `streamnet.c` / `streamfile.c` (requests on a thread, kfile reads).
- [ ] App: Library rows and pages, Settings page, + menu "About this song", Now Playing license line.
- [ ] Strings in the 8 language files; lang check.
- [ ] Preview scenes; 0 touch problems in every language.
- [ ] Docs (docs/online.md, README, controls, limitations, legal, building, settings).
- [ ] .nro and the test copy.
