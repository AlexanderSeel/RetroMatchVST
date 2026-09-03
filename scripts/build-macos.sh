#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${CONFIG:-Release}"
JUCE_DIR="${JUCE_DIR:-$ROOT/extern/JUCE}"
BUILD_DIR="$ROOT/build-macos"
RUN_TESTS="${RUN_TESTS:-0}"
TESTS_FLAG="OFF"
[[ "$RUN_TESTS" == "1" ]] && TESTS_FLAG="ON"

command -v cmake >/dev/null || { echo "cmake not found. See docs/BUILD.md"; exit 1; }
command -v git >/dev/null || { echo "git not found. Install Xcode command line tools."; exit 1; }
xcodebuild -version >/dev/null || { echo "Xcode is required for AU/VST3 builds."; exit 1; }

if [[ ! -f "$JUCE_DIR/CMakeLists.txt" ]]; then
  git clone --depth 1 --branch 9.0.1 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -G Xcode \
  -DRETROMATCH_JUCE_DIR="$JUCE_DIR" \
  -DRETROMATCH_COPY_PLUGIN=OFF \
  -DRETROMATCH_BUILD_TESTS="$TESTS_FLAG" \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"

cmake --build "$BUILD_DIR" --config "$CONFIG" \
  --target RetroMatchSynth_VST3 RetroMatchSynth_AU RetroMatchSynth_Standalone --parallel

if [[ "$RUN_TESTS" == "1" ]]; then
  cmake --build "$BUILD_DIR" --config "$CONFIG" --target RetroMatchTests --parallel
  ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure
fi

ARTEFACTS="$BUILD_DIR/RetroMatchSynth_artefacts/$CONFIG"
echo "Build complete."
echo "VST3: $ARTEFACTS/VST3/RetroMatch Synth.vst3"
echo "AU:   $ARTEFACTS/AU/RetroMatch Synth.component"
echo "App:  $ARTEFACTS/Standalone/RetroMatch Synth.app"
