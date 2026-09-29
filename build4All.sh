#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
# rm -rf ./build
cmake -S . -B "./build"
cmake --build "./build" --config Release
cd ./ArmCaveHook-Arcplugins/binaries/
../../build/armcave ./Arc-mobile.iOS-iPadOS -o ./Arc-mobile.patched --plugins ../plugins/apple
../../build/armcave ./libcocos2dcpp.so -o ./libcocos2dcpp.so.patched --plugins ../plugins/android
