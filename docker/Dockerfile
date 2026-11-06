# GCCSDK 4.7.4 cross compiler plus host tools for cross-building the X server.
FROM riscosdotinfo/riscos-gccsdk-4.7:latest
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      pkg-config autoconf automake libtool xutils-dev xfonts-utils x11-xkb-utils xkb-data \
      bzip2 xz-utils wget ca-certificates python3 file \
    && rm -rf /var/lib/apt/lists/*
