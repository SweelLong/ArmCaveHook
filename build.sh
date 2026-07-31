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

CONF="${CONF:-armcave.conf}"
[ -f "$CONF" ] || fail "$CONF was not found"

if [ ! -d "ArmCaveHook-Arcplugins/.git" ] && [ ! -f "ArmCaveHook-Arcplugins/.git" ]; then
    git submodule update --init --depth 1 2>/dev/null || \
        fail "ArmCaveHook-Arcplugins submodule not found. Run: git submodule update --init"
fi

conf_value() {
    section=$1
    key=$2
    if [ -z "$section" ]; then
        sed -n '/^\[/q; s/^[[:space:]]*'"$key"'[[:space:]]*=[[:space:]]*//p' "$CONF" | tail -n 1
    else
        sed -n '/^\['"$section"'\]/,/^\[/{
            /^\[/d
            s/^[[:space:]]*'"$key"'[[:space:]]*=[[:space:]]*//p
        }' "$CONF" | tail -n 1
    fi
}

get_enabled_profiles() {
    awk '
        /^\[/ {
            if (profile != "" && enabled) print profile
            profile = substr($0, 2, length($0) - 2)
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", profile)
            enabled = 0
        }
        /^[[:space:]]*enable[[:space:]]*=[[:space:]]*true/ && profile != "" {
            enabled = 1
        }
        END {
            if (profile != "" && enabled) print profile
        }
    ' "$CONF"
}

build_dir=$(conf_value "" build_dir)

[ -n "$build_dir" ] || fail "build_dir is not set in $CONF"

profiles=$(get_enabled_profiles)
[ -n "$profiles" ] || fail "No enabled profiles found in $CONF (set enable = true in a [section])"

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

for profile in $profiles; do
    input=$(conf_value "$profile" input)
    output=$(conf_value "$profile" output)
    plugins=$(conf_value "$profile" plugins)
    wl=$(conf_value "$profile" plugin_whitelist)
    bl=$(conf_value "$profile" plugin_blacklist)

    [ -n "$input" ] || fail "input is not set in [$profile] section of $CONF"
    [ -n "$output" ] || fail "output is not set in [$profile] section of $CONF"
    [ -n "$plugins" ] || fail "plugins is not set in [$profile] section of $CONF"

    echo "=== ArmCaveHook: [$profile] ==="
    echo "  input:   $input"
    echo "  output:  $output"
    echo "  plugins: $plugins"

    set -- "$cli" "$input" -o "$output" --plugins "$plugins"
    [ -z "$wl" ] || set -- "$@" --plugin-whitelist "$wl"
    [ -z "$bl" ] || set -- "$@" --plugin-blacklist "$bl"

    "$@"
    echo ""
done

echo "All profiles completed."
