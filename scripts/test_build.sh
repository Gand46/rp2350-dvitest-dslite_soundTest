#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

if [[ -z "${PICO_SDK_PATH:-}" ]]; then
  echo "ERROR: PICO_SDK_PATH is not set." >&2
  echo "Run: source scripts/setup_testing_env.sh /path/to/pico-sdk" >&2
  exit 1
fi

if [[ ! -f "${PICO_SDK_PATH}/pico_sdk_init.cmake" ]]; then
  echo "ERROR: PICO_SDK_PATH does not point to a valid pico-sdk checkout." >&2
  exit 1
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR:-Ninja}" -DPICO_PLATFORM="${PICO_PLATFORM:-rp2350}"
cmake --build "${BUILD_DIR}"
