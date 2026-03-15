#!/usr/bin/env bash
set -euo pipefail

# Configure local environment variables for reproducible firmware builds/tests.
# Usage:
#   source scripts/setup_testing_env.sh /path/to/pico-sdk

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  echo "ERROR: source this script so variables persist in your shell:" >&2
  echo "  source scripts/setup_testing_env.sh /path/to/pico-sdk" >&2
  exit 1
fi

SDK_PATH="${1:-${PICO_SDK_PATH:-}}"
if [[ -z "${SDK_PATH}" ]]; then
  echo "ERROR: missing pico-sdk path." >&2
  echo "Pass it as first arg or export PICO_SDK_PATH first." >&2
  return 1
fi

if [[ ! -f "${SDK_PATH}/pico_sdk_init.cmake" ]]; then
  echo "ERROR: invalid pico-sdk path: ${SDK_PATH}" >&2
  echo "Expected file not found: ${SDK_PATH}/pico_sdk_init.cmake" >&2
  return 1
fi

export PICO_SDK_PATH="$(cd "${SDK_PATH}" && pwd)"
export PICO_PLATFORM="${PICO_PLATFORM:-rp2350}"
export CMAKE_GENERATOR="${CMAKE_GENERATOR:-Ninja}"

echo "Configured test environment:"
echo "  PICO_SDK_PATH=${PICO_SDK_PATH}"
echo "  PICO_PLATFORM=${PICO_PLATFORM}"
echo "  CMAKE_GENERATOR=${CMAKE_GENERATOR}"
