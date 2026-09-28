#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
# rm -rf ./build
cmake -S . -B "./build"
cmake --build "./build" --config Release
cd ./ArmCaveHook-Arcplugins/binaries/
../../build/armcave ./Arc-mobile.mac-catalyst -o ./Arc-mobile.patched --plugins ../plugins/apple
cd ~/Library/Containers/io.playcover.PlayCover/Applications/moe.afe.arc.app
mv ~/Projects/ArmCaveHook/ArmCaveHook-Arcplugins/binaries/Arc-mobile.patched ./Arc-mobile
chmod +x ./Arc-mobile
xattr -c ./Arc-mobile
codesign -f -s - ./Arc-mobile
./Arc-mobile
lldb ./Arc-mobile -o "run"
