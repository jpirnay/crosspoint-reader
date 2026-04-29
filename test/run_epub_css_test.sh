#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/epub_css"
BINARY="$BUILD_DIR/CssParserTest"
PLATFORMIO_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
ARDUINO_FRAMEWORK_DIR="$PLATFORMIO_DIR/packages/framework-arduinoespressif32"

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/epub_css/CssParserTest.cpp"
  "$ROOT_DIR/lib/Epub/Epub/css/CssParser.cpp"
  "$ROOT_DIR/lib/hal/HalStorage.cpp"
  "$ROOT_DIR/lib/Logging/Logging.cpp"
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
  -I"$ROOT_DIR/test/shims"
  -I"$ROOT_DIR"
  -I"$ROOT_DIR/lib"
  -I"$ROOT_DIR/lib/hal"
  -I"$ROOT_DIR/lib/Logging"
  -I"$ARDUINO_FRAMEWORK_DIR/cores/esp32"
  -I"$ARDUINO_FRAMEWORK_DIR/variants/esp32c3"
)

c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"

"$BINARY" "$@"
