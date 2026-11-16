# Host OpenGL ES (Mesa's software renderer) on top of the devkit image, to
# check the GPU drawing backend against the CPU one without a console.
FROM ounce-devkit
RUN apt-get update && apt-get install -y --no-install-recommends \
        libegl-dev libgles-dev libegl-mesa0 libgl1-mesa-dri mesa-utils-bin \
    && rm -rf /var/lib/apt/lists/*
