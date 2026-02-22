#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/host_tests"

mkdir -p "$BUILD_DIR"

gcc -c "$ROOT_DIR/lib/md4c/md4c.c" -I"$ROOT_DIR/lib/md4c" -o "$BUILD_DIR/md4c.o"
gcc -c "$ROOT_DIR/lib/md4c/entity.c" -I"$ROOT_DIR/lib/md4c" -o "$BUILD_DIR/entity.o"

g++ -std=c++20 -O2 \
  -I"$ROOT_DIR" \
  -I"$ROOT_DIR/test/mock" \
  -I"$ROOT_DIR/lib/FsHelpers" \
  -I"$ROOT_DIR/lib/Markdown" \
  -I"$ROOT_DIR/lib/md4c" \
  -I"$ROOT_DIR/lib/Serialization" \
  -I"$ROOT_DIR/include" \
  -I"$ROOT_DIR/src" \
  -I"$ROOT_DIR/.pio/libdeps/default/ArduinoJson/src" \
  "$ROOT_DIR/test/HostTests.cpp" \
  "$ROOT_DIR/lib/FsHelpers/FsHelpers.cpp" \
  "$ROOT_DIR/lib/Markdown/MarkdownParser.cpp" \
  "$ROOT_DIR/src/util/InputValidation.cpp" \
  "$ROOT_DIR/src/util/PathUtils.cpp" \
  "$ROOT_DIR/src/CrossPointSettings.cpp" \
  "$ROOT_DIR/test/mock/JsonSettingsIO.cpp" \
  "$BUILD_DIR/md4c.o" \
  "$BUILD_DIR/entity.o" \
  -o "$BUILD_DIR/HostTests"

"$BUILD_DIR/HostTests"
