# What the network folder's live tests need on a PC: gcc, libcurl, libsmb2
# 6.0.0 (as smb/Dockerfile builds for the console) from its release, and the
# host FFmpeg 7.1 that the devkit image builds (ffmpeg/Dockerfile), copied
# from it: build kaiplayer-devkit first.
#   docker build -t kaiplayer-host-live tests/host_live
ARG DEVKIT_IMAGE=kaiplayer-devkit:latest
FROM ${DEVKIT_IMAGE} AS devkit

FROM debian:bookworm-slim

ARG LIBSMB2_VERSION=6.0.0
ARG LIBSMB2_SHA256=c65a7a2b7a6e967a4fd5b6168fca4f1f7d0362a3510767700609c770e3ead1e8

RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends gcc libc6-dev make cmake curl \
      ca-certificates libcurl4-openssl-dev util-linux && rm -rf /var/lib/apt/lists/*
WORKDIR /tmp/libsmb2
RUN curl -fsSL -o libsmb2.tar.gz "https://github.com/sahlberg/libsmb2/archive/refs/tags/v${LIBSMB2_VERSION}.tar.gz" \
 && echo "${LIBSMB2_SHA256}  libsmb2.tar.gz" | sha256sum -c - \
 && tar xzf libsmb2.tar.gz --strip-components=1 \
 && cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=OFF -DENABLE_EXAMPLES=OFF \
 && cmake --build build -j"$(nproc)" && cmake --install build && rm -rf /tmp/libsmb2
COPY --from=devkit /opt/ffmpeg-host /opt/ffmpeg-host
WORKDIR /w
