#!/bin/sh
# Renders the preview at 1440p twice: from the start, and after a first scene
# at 1080p (handheld, then docked), and compares the scenes after the first.
set -e
OUT=tools/gltest/build
sh tools/gltest/run.sh >/dev/null
for mode in direct later; do
    rm -f $OUT/hi_frame*.ppm
    if [ $mode = later ]; then export GLTEST_DOCKED_LATER=1; else unset GLTEST_DOCKED_LATER; fi
    (cd tests/preview && GLTEST_HEIGHT=1440 "../../$OUT/preview_gpu" "../../$OUT/gpu" assets/track.flac >/dev/null)
    mkdir -p $OUT/docked_$mode && mv $OUT/hi_frame*.ppm $OUT/docked_$mode/
done
