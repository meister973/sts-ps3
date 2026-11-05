# Everything needed to turn a player's own desktop-1.0.jar into a PS3 PKG.
#
# Self-contained and pinned: the ps3dev nightly (toolchain, PSL1GHT, portlibs)
# and Gradle are verified by hash, so anyone building this image gets the same
# tools. Based on the Balatro PS3 port's toolchain image.
FROM ubuntu:24.04

ARG PS3DEV_TARBALL=https://github.com/ps3dev/ps3dev/releases/download/nightly-2026-10-12/ps3dev-linux-X64.tar.gz
ARG PS3DEV_SHA256=76b1b3bc9ac260a4ece86da520225da7f2f4e334174e37b850093ff925eeb900
ARG GRADLE_VERSION=8.14.3
ARG GRADLE_SHA256=bd71102213493060956ec229d946beee57158dbd89d0e62b91bca0fa2c5f3531

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl file make git unzip \
        gcc g++ cmake \
        python3 python3-pil \
        openjdk-21-jdk-headless \
        zlib1g libelf1 libncurses6 \
    && rm -rf /var/lib/apt/lists/*

# cgcomp loads libCg at runtime and fails without it. Ubuntu ships libCg in multiverse.
RUN sed -i 's/^Components: main.*/Components: main universe multiverse/' /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get update \
    && apt-get install -y --no-install-recommends libcg=3.1.0013-5build1 \
    && rm -rf /var/lib/apt/lists/*

# Some nightlies wrap everything in one top directory, others do not.
RUN set -eux; \
    curl -fsSL --retry 3 -o /tmp/ps3dev.tar.gz "$PS3DEV_TARBALL"; \
    echo "$PS3DEV_SHA256  /tmp/ps3dev.tar.gz" | sha256sum -c -; \
    mkdir -p /tmp/unpack; \
    tar -xzf /tmp/ps3dev.tar.gz -C /tmp/unpack; \
    count=$(ls -A /tmp/unpack | wc -l); \
    if [ "$count" = "1" ] && [ -d "/tmp/unpack/$(ls -A /tmp/unpack)" ]; then \
        mv "/tmp/unpack/$(ls -A /tmp/unpack)" /usr/local/ps3dev; \
    else \
        mkdir -p /usr/local/ps3dev; \
        mv /tmp/unpack/* /usr/local/ps3dev/; \
    fi; \
    rm -rf /tmp/unpack /tmp/ps3dev.tar.gz

RUN set -eux; \
    curl -fsSL --retry 3 -o /tmp/gradle.zip "https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip"; \
    echo "$GRADLE_SHA256  /tmp/gradle.zip" | sha256sum -c -; \
    unzip -q /tmp/gradle.zip -d /opt; \
    rm /tmp/gradle.zip

ENV PS3DEV=/usr/local/ps3dev
ENV PSL1GHT=$PS3DEV
ENV PATH=/opt/gradle-${GRADLE_VERSION}/bin:$PATH:/usr/local/ps3dev/bin:/usr/local/ps3dev/ppu/bin:/usr/local/ps3dev/spu/bin

# A broken shader compiler fails the image build rather than a later game build.
RUN set -eux; \
    mkdir -p /tmp/cg; \
    echo 'void main(float4 p:POSITION,out float4 o:POSITION,uniform float4x4 m){o=mul(p,m);}' > /tmp/cg/t.vcg; \
    echo 'void main(float2 t:TEXCOORD0,out float4 c:COLOR,uniform sampler2D s){c=tex2D(s,t);}' > /tmp/cg/t.fcg; \
    cgcomp -v /tmp/cg/t.vcg /tmp/cg/t.vpo; \
    cgcomp -f /tmp/cg/t.fcg /tmp/cg/t.fpo; \
    rm -rf /tmp/cg

WORKDIR /work
