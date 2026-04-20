#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/url_utils"
BINARY="$BUILD_DIR/UrlUtilsTest"
PLATFORMIO_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
ARDUINO_FRAMEWORK_DIR="$PLATFORMIO_DIR/packages/framework-arduinoespressif32"

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/url_utils/UrlUtilsTest.cpp"
  "$ROOT_DIR/src/util/UrlUtils.cpp"
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
  -I"$ROOT_DIR/src"
  -I"$ARDUINO_FRAMEWORK_DIR/cores/esp32"
  -I"$ARDUINO_FRAMEWORK_DIR/variants/esp32c3"
)

c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"

"$BINARY" "$@"
