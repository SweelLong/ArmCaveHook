# ArmCaveHook

<p align="center">
  <img src="https://img.shields.io/badge/Arch-ARM64%20%7C%20AArch64-blue?logo=arm" alt="ARM64">
  <img src="https://img.shields.io/badge/Apple-Apple%20Mach--O-lightgrey?logo=apple" alt="Apple">
  <img src="https://img.shields.io/badge/Android-Android%20ELF-lightgrey?logo=android" alt="Android">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-blue?logo=cplusplus" alt="C++17">
  <img src="https://img.shields.io/badge/CMake-3.24+-blue?logo=cmake" alt="CMake">
  <img src="https://img.shields.io/badge/License-MIT-yellow?logo=opensourceinitiative" alt="License">
  <img src="https://img.shields.io/github/last-commit/SweelLong/ArmCaveHook?logo=git" alt="Last Commit">
  <img src="https://img.shields.io/github/repo-size/SweelLong/ArmCaveHook?logo=hackthebox" alt="Repo Size">
  <img src="https://img.shields.io/badge/Docs-English%20%7C%20中文-brightgreen?logo=readthedocs" alt="Docs">
</p>

English | [简体中文](README_CN.md)

ArmCaveHook is an ARM64/AArch64 static binary patch framework for 64-bit Apple
Mach-O and Android ELF binaries. It compiles C++ plugins, supports direct AArch64
assembly patches, creates isolated code and data segments, and writes the patched binary.

## Quick Start

```bash
git clone --recursive https://github.com/SweelLong/ArmCaveHook.git
cd ArmCaveHook
./build.sh
```

Select an enabled profile in `armcave.conf` before building.

```cpp
#include "armcave.h"

extern "C" int replacement(int value) { return value + 1; }

extern "C" void init(void) {
    hook_replace(0x100000498, replacement, w0);
}
```

## Documentation

- [Getting started](docs/getting-started.md)
- [Architecture](docs/architecture.md)
- [API reference](docs/api-reference.md)
- [Hook API](docs/hook-api.md)
- [Location and relocation](docs/location-and-relocation.md)
- [Framework analysis](docs/framework-analysis.md)
- [Patch Script](docs/patch-script.md)
- [Plugin SDK](docs/plugin-sdk.md)
- [Plugin data addresses](docs/plugin-data.md)
- [Segment naming](docs/segments.md)
- [Build configuration and diagnostics](docs/configuration-and-diagnostics.md)
- [Project status](docs/project-status.md)

## Requirements

- CMake 3.24 or newer
- C++17 compiler
- Clang and Clang++ for AArch64 plugin objects

## License

MIT
