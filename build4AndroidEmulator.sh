#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
# rm -rf ./build
cmake -S . -B "./build"
cmake --build "./build" --config Release
cd ./ArmCaveHook-Arcplugins/binaries/
../../build/armcave ./libcocos2dcpp.so -o ./libcocos2dcpp.so.patched --plugins ../plugins/android
adb devices && adb root
adb push ./libcocos2dcpp.so.patched /data/app/~~IhtGg5OipBp2GVjcfLYFiw==/moe.low.arc-MDj0Adgv_s5Ww9rJ7Yc4AQ==/lib/arm64/libcocos2dcpp.so
