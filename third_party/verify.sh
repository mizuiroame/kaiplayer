#!/bin/sh
# Checks the vendored files against the SHA-256 sums in VERSIONS.md.
set -eu
cd "$(dirname "$0")"
sha256sum -c <<'EOF'
7a6cc38b0afdb55cfc9eec991d9169d67beb8fd910be2995121510cdaa99c356  dr_mp3.h
3f010d3142a5b9df80a0bd8b325de54317468b1aff85abb257c86dbfefe9bd2a  dr_flac.h
7c9660d75361ad6175e6217c63a1075c9512445c06bc951197fc479179f76b39  dr_wav.h
b52cf518355f7b2d181f18b97b812f361ffeb26299d935c8cf29e842401d60a6  stb_vorbis.c
49d8df366296b203ca3211bc0672cf2a762135bf12710735b6292756b19dffd5  font8x8_basic.h
e0582e9a6b3f85dbb983c72d95c9a50c3547ad5e895d16ef167e20777d053f1b  font8x8_ext_latin.h
EOF
