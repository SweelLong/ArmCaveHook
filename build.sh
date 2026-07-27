#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

fail() {
    echo "Error: $*" >&2
    exit 1
}

need_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "$1 was not found in PATH"
}

conf_value() {
    key=$1
    sed -n "s/^${key}[[:space:]]*=[[:space:]]*//p" "$CONF" | tail -n 1
}

[ "$#" -eq 0 ] || fail "build.sh does not accept arguments; edit armcave.conf"

CONF=armcave.conf
[ -f "$CONF" ] || fail "$CONF was not found"

# Ensure ArmCaveHook-Arcplugins submodule is present
if [ ! -d "ArmCaveHook-Arcplugins/.git" ] && [ ! -f "ArmCaveHook-Arcplugins/.git" ]; then
    git submodule update --init --depth 1 2>/dev/null || \
        fail "ArmCaveHook-Arcplugins submodule not found. Run: git submodule update --init"
fi

input=$(conf_value input)
output=$(conf_value output)
plugins=$(conf_value plugins)
wl=$(conf_value plugin_whitelist)
bl=$(conf_value plugin_blacklist)
build_dir=$(conf_value build_dir)

[ -n "$input" ] || fail "input is not set in $CONF"
[ -n "$output" ] || fail "output is not set in $CONF"
[ -n "$plugins" ] || fail "plugins is not set in $CONF"
[ -n "$build_dir" ] || fail "build_dir is not set in $CONF"

need_tool cmake
need_tool clang
need_tool clang++

cmake -S . -B "$build_dir"
cmake --build "$build_dir" --config Release

cli="$build_dir/armcave"
if [ ! -x "$cli" ] && [ -x "$build_dir/Release/armcave" ]; then
    cli="$build_dir/Release/armcave"
fi
[ -x "$cli" ] || fail "built CLI not found under $build_dir"

set -- "$cli" "$input" -o "$output" --plugins "$plugins"
[ -z "$wl" ] || set -- "$@" --plugin-whitelist "$wl"
[ -z "$bl" ] || set -- "$@" --plugin-blacklist "$bl"
exec "$@"
