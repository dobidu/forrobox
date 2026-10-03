# Forró Box — the Linux PORTABLE build environment, and the clean machine that
# proves it (15-02). Two stages, both Ubuntu 22.04:
#
#   runtime  what a user's 22.04 has: the shared libraries the plugin loads,
#            plus Xvfb/xprop to run the suite headless. NO compiler, no -dev
#            package, no GCC 13 libstdc++ — so `ldd` and the suite prove the
#            binaries need nothing beyond the runtime set.
#   build    the runtime plus the toolchain: GCC 13 from the toolchain PPA (the
#            code is C++20), CMake 3.22.1 (22.04's own, the project's minimum),
#            Ninja, Python 3 and Node 20 for the design cross-check gates, and
#            JUCE's headers.
#
# glibc 2.35 is the floor the Linux package supports (Ubuntu 22.04+, Debian 12+,
# Fedora 36+). libstdc++/libgcc are linked STATICALLY (FORROBOX_PORTABLE_LINUX),
# so GCC 13's libstdc++ never becomes a runtime requirement.
# scripts/build-linux-portable.sh builds both targets and checks the result.

FROM ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
# The libraries juce_audio_devices (ALSA), juce_graphics (FreeType, fontconfig)
# and juce_gui_basics (X11, loaded at run time) need; Xvfb + xprop for the
# headless suite (scripts/headless-x.sh).
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libasound2 libfreetype6 libfontconfig1 \
      libx11-6 libxrandr2 libxinerama1 libxcursor1 libxext6 libxcomposite1 libxrender1 \
      xvfb xauth x11-utils \
 && rm -rf /var/lib/apt/lists/*

FROM runtime AS build
# GCC 13 from the Ubuntu toolchain PPA; CMake, Ninja, pkg-config; Python 3 for
# the gates; xz/curl/ca-certificates to fetch Node; JUCE's -dev headers for the
# same modules as above (no webkit, no curl: JUCE_WEB_BROWSER=0, JUCE_USE_CURL=0).
RUN apt-get update \
 && apt-get install -y --no-install-recommends software-properties-common gpg-agent \
 && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      gcc-13 g++-13 cmake ninja-build pkg-config make python3 xz-utils curl ca-certificates \
      libasound2-dev libfreetype-dev libfontconfig1-dev \
      libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev \
      libxcomposite-dev libxrender-dev \
 && rm -rf /var/lib/apt/lists/*

# Node 18+ runs the prototype's exportMIDI in the MIDI cross-check; 22.04 ships 12.
# The official tarball, version AND checksum pinned, so the image is reproducible
# and a tampered download fails the build.
ARG NODE_VERSION=20.20.2
ARG NODE_SHA256=df770b2a6f130ed8627c9782c988fda9669fa23898329a61a871e32f965e007d
RUN curl -fsSLo /tmp/node.tar.xz "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-x64.tar.xz" \
 && echo "${NODE_SHA256}  /tmp/node.tar.xz" | sha256sum -c - \
 && tar -xJf /tmp/node.tar.xz -C /usr/local --strip-components=1 \
 && rm /tmp/node.tar.xz \
 && node --version

ENV CC=gcc-13 CXX=g++-13
