#!/bin/sh
# Configures FFmpeg for KaiPlayer: only what plays AAC, ALAC and Opus files
# (MP4/M4A, ADTS .aac, Ogg), and the resampler. No GPL or non-free parts, no
# network code, no programs (KaiPlayer reads the files itself, through kfile).
#   sh configure.sh switch2   for the console, into the devkit's portlibs
#   sh configure.sh host      for the host tests, into /opt/ffmpeg-host
# Both builds take the same options, so the tests decode what the console does.
set -eu

common="--disable-shared --enable-static --enable-pic
  --disable-runtime-cpudetect --disable-programs --disable-doc --disable-debug
  --disable-autodetect --disable-network --disable-everything
  --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc
  --enable-avcodec --enable-avformat --enable-swresample
  --enable-decoder=aac,alac,opus
  --enable-parser=aac,opus
  --enable-demuxer=mov,ogg,aac"

case "${1:-}" in
switch2)
    # In the shell that runs make too, which needs the same PATH:
    # . /opt/devkitpro/switch2vars.sh first.
    : "${PORTLIBS_PREFIX:?source /opt/devkitpro/switch2vars.sh first}"
    # shellcheck disable=SC2086
    ./configure --prefix="$PORTLIBS_PREFIX" \
        --enable-cross-compile --cross-prefix=aarch64-none-elf- \
        --arch=aarch64 --target-os=horizon --enable-neon \
        --extra-cflags="$CPPFLAGS $CFLAGS -D_GNU_SOURCE" \
        --extra-ldflags="$LDFLAGS" \
        $common
    ;;
host)
    # No nasm in the image: the plain C code, slower, which the tests don't mind.
    # shellcheck disable=SC2086
    ./configure --prefix=/opt/ffmpeg-host --disable-x86asm $common
    ;;
*)
    echo "usage: configure.sh switch2|host" >&2
    exit 2
    ;;
esac

# The siblings' rule: the build fails if anything GPL or non-free got in.
grep -q 'define CONFIG_GPL 0' config.h
grep -q 'define CONFIG_NONFREE 0' config.h
