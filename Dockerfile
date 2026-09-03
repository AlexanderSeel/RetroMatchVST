FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG JUCE_TAG=9.0.1
ARG CONFIG=Release

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    python3 \
    ca-certificates \
    pkg-config \
    libasound2-dev \
    libjack-jackd2-dev \
    ladspa-sdk \
    libfreetype-dev \
    libfontconfig1-dev \
    libx11-dev \
    libxcomposite-dev \
    libxcursor-dev \
    libxext-dev \
    libxinerama-dev \
    libxrandr-dev \
    libxrender-dev \
    libxi-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch ${JUCE_TAG} https://github.com/juce-framework/JUCE.git /opt/JUCE

WORKDIR /src
COPY . .

RUN python3 scripts/static-check.py
RUN cmake -S . -B /build \
      -DCMAKE_BUILD_TYPE=${CONFIG} \
      -DRETROMATCH_JUCE_DIR=/opt/JUCE \
      -DRETROMATCH_COPY_PLUGIN=OFF \
      -DRETROMATCH_BUILD_TESTS=ON \
    && cmake --build /build --config ${CONFIG} --target RetroMatchSynth_VST3 RetroMatchSynth_Standalone RetroMatchTests --parallel \
    && ctest --test-dir /build -C ${CONFIG} --output-on-failure

RUN mkdir -p /out \
    && cp -a /build/RetroMatchSynth_artefacts/${CONFIG}/VST3 /out/ \
    && cp -a /build/RetroMatchSynth_artefacts/${CONFIG}/Standalone /out/

CMD ["/bin/bash"]
