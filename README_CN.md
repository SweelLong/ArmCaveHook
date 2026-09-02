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

[English](README.md) | 简体中文

ArmCaveHook 是面向 64 位 Apple Mach-O 与 Android ELF 二进制的 ARM64/AArch64 静态补丁框架。
它编译 C++ 插件，支持直接写 AArch64 汇编 patch，创建相互隔离的代码段和数据段，并输出已修补的二进制文件。

## 快速开始

```bash
git clone --recursive https://github.com/SweelLong/ArmCaveHook.git
cd ArmCaveHook
./build.sh
```

构建前，请在 `armcave.conf` 中启用需要的平台 profile。

```cpp
#include "armcave.h"

extern "C" int replacement(int value) { return value + 1; }

extern "C" void init(void) {
    hook_replace(0x100000498, replacement, w0);
}
```

当前已支持 Apple Mach-O 与 Android ELF 注入、AArch64 解码/重定位/CFG 和函数分析、远跳序列、
隔离的插件代码段与数据段、符号和 PLT/GOT 查找、字节签名、Mach-O chained fixup、Objective-C
与 Swift metadata、`patch.toml` 以及结构化诊断。

后续计划包括代码签名流程说明、字节签名稳定性指导、可选的跨版本地址辅助工具、轻量 C++ 插件
工具集，以及对 Android ELF 的 DT_RELR 和 `eh_frame` 等支持扩展。

## 文档

- [架构](docs/architecture_CN.md)
- [API 参考](docs/api-reference_CN.md)
- [Hook API](docs/hook-api_CN.md)
- [段名规则](docs/segments_CN.md)
- [构建配置与诊断](docs/configuration-and-diagnostics_CN.md)

## 环境要求

- CMake 3.24 或更新版本
- C++17 编译器
- 用于生成 AArch64 插件对象的 Clang 和 Clang++

## License

MIT
