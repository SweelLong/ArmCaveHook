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

exec python3 armcave.py "$input" -o "$output" \
    --plugins "$plugins" \
    ${wl:+--plugin-whitelist "$wl"} \
    ${bl:+--plugin-blacklist "$bl"}
