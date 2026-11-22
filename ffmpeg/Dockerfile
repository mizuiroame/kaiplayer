# FFmpeg 7.1 under the LGPL 2.1, for KaiPlayer's AAC, ALAC and Opus files:
# their decoders, the MP4/M4A, Ogg and ADTS demuxers and the resampler
# (configure.sh has the options). Built twice from the same checked release:
# for Switch 2 into portlibs/switch2, and for the host tests into
# /opt/ffmpeg-host (tests/host_live/Dockerfile copies that one too).
# Over the image with libsmb2 (smb/Dockerfile):
#   docker build -t kaiplayer-devkit:smb smb
#   docker build --build-arg DEVKIT_IMAGE=kaiplayer-devkit:smb -t kaiplayer-devkit ffmpeg
ARG DEVKIT_IMAGE=kaiplayer-devkit:smb
FROM ${DEVKIT_IMAGE}

ARG FFMPEG_VERSION=7.1
ARG FFMPEG_SHA256=40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6

WORKDIR /tmp/ffmpeg
COPY horizon.patch configure.sh ./
RUN curl -fsSL -o ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz" \
 && echo "${FFMPEG_SHA256}  ffmpeg.tar.xz" | sha256sum -c - \
 && mkdir switch2 host \
 && tar xf ffmpeg.tar.xz -C switch2 --strip-components=1 \
 && tar xf ffmpeg.tar.xz -C host --strip-components=1 \
 && (cd switch2 && patch -Np1 -i ../horizon.patch)

RUN . /opt/devkitpro/switch2vars.sh \
 && cd switch2 && sh ../configure.sh switch2 && make -j"$(nproc)" && make install \
 && cp COPYING.LGPLv2.1 "$PORTLIBS_PREFIX/include/libavcodec/"

RUN cd host && sh ../configure.sh host && make -j"$(nproc)" && make install \
 && cp COPYING.LGPLv2.1 /opt/ffmpeg-host/ \
 && rm -rf /tmp/ffmpeg

WORKDIR /src
