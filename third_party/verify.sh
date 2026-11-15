#!/bin/sh
# Checks the vendored files and fonts against the SHA-256 sums in VERSIONS.md.
set -eu
cd "$(dirname "$0")"
sha256sum -c <<'EOF'
7a6cc38b0afdb55cfc9eec991d9169d67beb8fd910be2995121510cdaa99c356  dr_mp3.h
3f010d3142a5b9df80a0bd8b325de54317468b1aff85abb257c86dbfefe9bd2a  dr_flac.h
7c9660d75361ad6175e6217c63a1075c9512445c06bc951197fc479179f76b39  dr_wav.h
b52cf518355f7b2d181f18b97b812f361ffeb26299d935c8cf29e842401d60a6  stb_vorbis.c
deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91  stb_image.h
428cfe93520f2aed5e413a220b0db5bba5b17e2ebe5a64e4a8efa9eeeb67ec5f  stb_truetype.h
cee9892f9f0cc8fe882c9e9537ee6a89621d86ee7ceaf70b02e2b2b1c25c061a  NotoSans-OFL.txt
478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823  ../data/font_regular.ttf
1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913  ../data/font_bold.ttf
EOF
