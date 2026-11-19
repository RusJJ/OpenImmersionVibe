#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ndk-build NDK_PROJECT_PATH="$SCRIPT_DIR" APP_BUILD_SCRIPT="$SCRIPT_DIR/Android.mk" NDK_APPLICATION_MK="$SCRIPT_DIR/Application.mk" NDK_DEBUG=0 -j4 "$@"
