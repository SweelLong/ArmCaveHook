#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

usage() {
    cat <<'EOF'
Usage: ./build.sh [options]
  --input <path>                 Input Mach-O or ELF
  --output <path>                Patched output (default: <input>.patched)
  --plugins <dir>                Plugin directory (default: plugins)
  --plugin-whitelist <names>     Comma-separated plugin names
  --plugin-blacklist <names>     Comma-separated plugin names
  --build-dir <dir>              CMake build directory (default: build)
  -h, --help                     Show this help

Options override ARMCAVE_INPUT/OUTPUT/PLUGINS/PLUGIN_WHITELIST/
PLUGIN_BLACKLIST/BUILD_DIR, which override armcave.conf.
EOF
}

fail() {
    echo "Error: $*" >&2
    exit 1
}

need_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "$1 was not found in PATH"
}

conf_value() {
    key=$1
    [ -f "$CONF" ] || return 0
    sed -n "s/^${key}[[:space:]]*=[[:space:]]*//p" "$CONF" | tail -n 1
}

CONF=${ARMCAVE_CONF:-armcave.conf}
input=${ARMCAVE_INPUT:-}
output=${ARMCAVE_OUTPUT:-}
plugins=${ARMCAVE_PLUGINS:-}
wl=${ARMCAVE_PLUGIN_WHITELIST:-}
bl=${ARMCAVE_PLUGIN_BLACKLIST:-}
build_dir=${BUILD_DIR:-build}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --input) [ "$#" -ge 2 ] || fail "$1 requires a value"; input=$2; shift 2 ;;
        --output) [ "$#" -ge 2 ] || fail "$1 requires a value"; output=$2; shift 2 ;;
        --plugins) [ "$#" -ge 2 ] || fail "$1 requires a value"; plugins=$2; shift 2 ;;
        --plugin-whitelist) [ "$#" -ge 2 ] || fail "$1 requires a value"; wl=$2; shift 2 ;;
        --plugin-blacklist) [ "$#" -ge 2 ] || fail "$1 requires a value"; bl=$2; shift 2 ;;
        --build-dir) [ "$#" -ge 2 ] || fail "$1 requires a value"; build_dir=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) fail "unknown option: $1" ;;
    esac
done

[ -n "$input" ] || input=$(conf_value input)
[ -n "$output" ] || output=$(conf_value output)
[ -n "$plugins" ] || plugins=$(conf_value plugins)
[ -n "$wl" ] || wl=$(conf_value plugin_whitelist)
[ -n "$bl" ] || bl=$(conf_value plugin_blacklist)
: "${plugins:=plugins}"
[ -n "$input" ] || fail "input is not set; use --input, ARMCAVE_INPUT, or $CONF"
: "${output:=${input}.patched}"

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
