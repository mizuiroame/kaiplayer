#!/bin/sh
# Renders the UI preview with the CPU backend and with the GPU one (Mesa's
# software OpenGL ES), then compares each scene. Run in the kaiplayer-gltest
# image from the repository root:
#   docker build -t kaiplayer-gltest tools/gltest
#   docker run --rm -v "$PWD:/k" -w /k kaiplayer-gltest sh tools/gltest/run.sh
set -e
PREVIEW=tests/preview
OUT=tools/gltest/build
mkdir -p "$OUT/cpu" "$OUT/gpu"
make -s -C "$PREVIEW" build/font_regular_ttf.c build/font_bold_ttf.c
UI_SRC=$(make -s -C "$PREVIEW" --eval='print-ui: ; @echo $(UI_SRC)' print-ui)
SRC=""
for f in $UI_SRC; do SRC="$SRC source/$f"; done
FLAGS="-std=gnu11 -O1 -Isource -Ithird_party -I$PREVIEW/build -w"
gcc $FLAGS "$PREVIEW/preview.c" "$PREVIEW/font_system_host.c" $SRC \
    "$PREVIEW/build/font_regular_ttf.c" "$PREVIEW/build/font_bold_ttf.c" -o "$OUT/preview_cpu" -lm
gcc $FLAGS -DKAIPLAYER_GPU "$PREVIEW/preview.c" "$PREVIEW/font_system_host.c" $SRC source/gfx_gl.c \
    tools/gltest/gfx_gl_host.c "$PREVIEW/build/font_regular_ttf.c" "$PREVIEW/build/font_bold_ttf.c" \
    -o "$OUT/preview_gpu" -lm -lEGL -lGLESv2
(cd "$PREVIEW" && "../../$OUT/preview_cpu" "../../$OUT/cpu" assets/track.flac >/dev/null)
(cd "$PREVIEW" && "../../$OUT/preview_gpu" "../../$OUT/gpu" assets/track.flac >/dev/null)
echo "rendered $(ls "$OUT/gpu" | grep -c ppm) scenes on each"
