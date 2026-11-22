# Security policy

## Supported versions

Only the [latest release](https://github.com/mizuiroame/kaiplayer/releases/latest)
gets security fixes. Please check that a problem still happens with it before
reporting.

## Reporting a vulnerability

Please don't open a public issue for a security problem. Report it privately
instead: on this repository's **Security** tab, choose **Report a
vulnerability**. Say which version you used, what you did and what happened,
and, if you can, attach the file or the server answer that shows it (a song, a
cover, a playlist, a WebDAV listing, a Navidrome answer).

KaiPlayer is a hobby project kept by one person. Reports are read and answered
as time allows; once a fix is released, the report can be made public, and
you're credited in the release notes unless you'd rather not be.

## Scope

In scope: KaiPlayer's own code and how it uses its libraries. For instance, a
file on the SD card, a network folder or a Navidrome server that can crash
KaiPlayer or make it run code, read or write files outside what it should, or
send a password or token where it shouldn't.

Out of scope: kaiHEN, the console's system software, the servers themselves
(Samba, Windows sharing, WebDAV servers, Navidrome) and bugs that belong to a
bundled library alone (please report those upstream too; KaiPlayer updates
them when there's a fix).

## How KaiPlayer handles your data and untrusted input

See [docs/security.md](docs/security.md): what it connects to, how the network
folder's password and the Navidrome sign-in are kept, the limits on what it
reads, and how it's tested.
