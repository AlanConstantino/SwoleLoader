FROM docker.io/devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282

ARG NEWLIB_COMMIT=8e4d4166b043622b1fe463dbdb8255a05f0d56b2
ARG LIBNX_COMMIT=b156b4774fb2df7b93402169509266da7547fc4c
ARG BUILD_JOBS=4

RUN apt-get update \
 && apt-get install -y --no-install-recommends ccache ninja-build texinfo \
 && rm -rf /var/lib/apt/lists/*

RUN git init /tmp/newlib \
 && git -C /tmp/newlib remote add origin https://github.com/R-YaTian/newlib.git \
 && git -C /tmp/newlib fetch --depth 1 origin "$NEWLIB_COMMIT" \
 && git -C /tmp/newlib checkout --detach FETCH_HEAD \
 && cd /tmp/newlib && bash build-libgloss-local.sh \
 && git init /tmp/libnx \
 && git -C /tmp/libnx remote add origin https://github.com/switchbrew/libnx.git \
 && git -C /tmp/libnx fetch --depth 1 origin "$LIBNX_COMMIT" \
 && git -C /tmp/libnx checkout --detach FETCH_HEAD \
 && git -C /tmp/libnx submodule update --init --recursive \
 && make -C /tmp/libnx install -j"$BUILD_JOBS" \
 && rm -rf /tmp/newlib /tmp/libnx

RUN git config --system --add safe.directory /src

ENV CMAKE_GENERATOR=Ninja \
    CMAKE_C_COMPILER_LAUNCHER=ccache \
    CMAKE_CXX_COMPILER_LAUNCHER=ccache \
    CCACHE_DIR=/ccache \
    CCACHE_MAXSIZE=2G \
    CCACHE_BASEDIR=/src \
    CCACHE_COMPILERCHECK=content

WORKDIR /src
