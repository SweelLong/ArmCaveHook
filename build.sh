#!/bin/sh
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
cd "$DIR"

CONF="armcave.conf"
if [ ! -f "$CONF" ]; then
    echo "Error: $CONF not found" >&2
    exit 1
fi

input=$(sed -n 's/^input[[:space:]]*=[[:space:]]*//p' "$CONF")
output=$(sed -n 's/^output[[:space:]]*=[[:space:]]*//p' "$CONF")
plugins=$(sed -n 's/^plugins[[:space:]]*=[[:space:]]*//p' "$CONF")
wl=$(sed -n 's/^plugin_whitelist[[:space:]]*=[[:space:]]*//p' "$CONF")
bl=$(sed -n 's/^plugin_blacklist[[:space:]]*=[[:space:]]*//p' "$CONF")
: "${plugins:=plugins}"

BUILD_DIR="${BUILD_DIR:-build}"
CLI="$BUILD_DIR/armcave"
cmake -S . -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --config Release

if [ ! -x "$CLI" ] && [ -x "$BUILD_DIR/Release/armcave" ]; then
    CLI="$BUILD_DIR/Release/armcave"
fi
if [ ! -x "$CLI" ]; then
    echo "Error: built CLI not found at $CLI" >&2
    exit 1
fi

set -- "$CLI" "$input" -o "$output" --plugins "$plugins"
if [ -n "$wl" ]; then
    set -- "$@" --plugin-whitelist "$wl"
fi
if [ -n "$bl" ]; then
    set -- "$@" --plugin-blacklist "$bl"
fi

exec "$@"
