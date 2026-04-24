#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/kosync_xpath"
BINARY="$BUILD_DIR/KOSyncXPathTest"
PLATFORMIO_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
ARDUINO_FRAMEWORK_DIR="$PLATFORMIO_DIR/packages/framework-arduinoespressif32"

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/kosync_xpath/KOSyncXPathTest.cpp"
  "$ROOT_DIR/lib/KOReaderSync/ChapterXPathForwardMapper.cpp"
  "$ROOT_DIR/lib/KOReaderSync/ChapterXPathReverseMapper.cpp"
  "$ROOT_DIR/lib/KOReaderSync/ChapterXPathIndexerInternal.cpp"
  "$ROOT_DIR/lib/Epub/Epub/htmlEntities.cpp"
  "$ROOT_DIR/lib/Utf8/Utf8.cpp"
)

CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  -pedantic
  -fno-exceptions
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DDESTRUCTOR_CLOSES_FILE=1
  -DXML_GE=0
  -DXML_CONTEXT_BYTES=1024
  -DUSE_UTF8_LONG_NAMES=1
  -I"$ROOT_DIR/test/shims"
  -I"$ROOT_DIR"
  -I"$ROOT_DIR/lib"
  -I"$ROOT_DIR/lib/KOReaderSync"
  -I"$ROOT_DIR/lib/Epub"
  -I"$ROOT_DIR/lib/Utf8"
  -I"$ROOT_DIR/lib/Logging"
  -I"$ARDUINO_FRAMEWORK_DIR/cores/esp32"
  -I"$ARDUINO_FRAMEWORK_DIR/variants/esp32c3"
)

c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -lexpat -o "$BINARY"

"$BINARY" "$@"
