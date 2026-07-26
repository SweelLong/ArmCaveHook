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

中文 | [English](README_EN.md)

ArmCaveHook 是一个 AArch64 静态 hook 框架。插件使用 `.cpp` 编写，框架把插件编译成 cave 代码段，再按插件入口声明修改目标二进制。

## 插件规范

插件开头必须包含标准库头文件：

```cpp
#include "armcave.h"
```

顶层只放 include、可选的 `#define SEGMENT_NAME`、类型声明、函数声明、全局变量、函数定义和 `init(void)`。除变量和声明外，不要把执行逻辑写在函数外面。对二进制的任何修改都写进 `init(void)`。

```cpp
#include "armcave.h"

#define SEGMENT_NAME testhook

extern "C" int replacement(int a, int b) {
    logf("a=%d b=%d\n", a, b);
    return a + b;
}

void init(void) {
    hook_replace(0x100000498, replacement, w0, w1);
}
```

`SEGMENT_NAME` 是插件段名。Mach-O 使用 `__testhook`，ELF 使用 `.testhook`。每个插件只生成一个 segment；框架会先汇总该插件全部 hook_replace dispatcher、寄存器 wrapper、编译后代码、常量数据和重定位所需空间，再创建最终大小的 segment。插件代码和常量只存放一份，多个 `hook_replace`/`hook_detour` 共用它们。

## 标准 API

| API | 作用 |
|---|---|
| `hook_replace(addr, handler, ...)` | 用 handler 替代目标函数；handler 返回后直接离开原函数。末尾可传寄存器名，例如 `x0, x1`。 |
| `hook_detour(addr, handler, ...)` | 在目标地址处先调用 handler，再执行被覆盖的原指令并回到原函数；末尾可传寄存器名，例如 `x20`。 |
| `patch_asm(addr, "instruction"[, "expected"])` | 汇编并写入 AArch64 指令；提供 `expected` 时，仅在等长的原指令序列匹配时写入。 |
| `bind_obj_by_sym(type, name, symbol)` | 将对象声明绑定到目标二进制里的符号，包括全局变量和静态对象。 |
| `bind_func_by_sym(ret, name, args, symbol)` | 将函数声明绑定到目标二进制里的符号。 |
| `bind_func_by_addr(ret, name, args, addr)` | 将函数声明绑定到固定虚拟地址，patch 后生成相对 `BL/B` relocation。 |
| `resolve_addr(va)` | 将 VMA 转为运行时地址（ADRP+PAGEOFF12，抗 ASLR），用于数据地址引用。 |
| `read_mem<T>(addr)` / `write_mem<T>(addr, value)` | 读写目标进程内存。 |
| `resolve_vfunc(obj, offset)` | 读取对象虚表中指定偏移的函数指针。 |
| `read_typeinfo(obj)` | 读取 Itanium C++ ABI vtable 前的 typeinfo 指针。 |
| `armcave_itoa(buf, value)` | 将整数写入缓冲区并返回字符数。 |
| `armcave_json_value(json, key, out, size)` | 从数字 key 的 JSON 对象中读取字符串值。 |
| `armcave_apple_string_make(text)` | 构造 Apple 24 字节短字符串参数。 |
| `armcave_apple_string_data(value)` | 读取 Apple 字符串的实际字符地址。 |
| `armcave_apple_file_manager_get(manager, path)` | 调用 Apple file manager 的资源读取方法。 |
| `logf(fmt, ...)` | 简单日志输出；末尾参数对应格式字符串，例如 `logf("value=%d", value)`。 |
| `u8/u16/u32/u64/i8/i16/i32/i64/addr_t` | 基础类型别名。 |

传入 hook 寄存器后，框架会生成 wrapper，把这些寄存器移动到标准 AArch64 调用参数寄存器。

`hook_replace` 和 `hook_detour` 都是静态写跳转到 code cave，但控制流不同：

```text
hook_replace: target -> cave -> handler -> RET
hook_detour:  target -> cave -> handler -> 原始被覆盖指令 -> target + 4
```

因此 `hook_replace` 适合完全替代一个函数或入口点，`hook_detour` 适合在原逻辑中插入处理并继续执行。`hook_detour` 的 hook 点应放在目标函数已经把关键状态保存到 callee-saved 寄存器之后；handler 应遵守 AArch64 调用约定，不要依赖 caller-saved 寄存器在返回后保持不变。

## 指令注入

```cpp
#define SEGMENT_NAME patchseg

void init(void) {
    patch_asm(0x100500000, "NOP");
    patch_asm(0x100500004, "MOV W0, #1; RET", "NOP; NOP");
}
```

`patch_asm` 用来写 AArch64 汇编文本；需要保护版本差异时，使用带 `expected` 的形式。

默认跳转使用 AArch64 `B` 指令。`B` 是 26-bit 相对跳转，按 4 字节指令对齐计算，范围是当前位置前后 128 MiB。Mach-O hook cave 入口会先执行 `XPACLRI` 再保存 `x29/x30`，用于清理带 PAC 签名位的返回地址，避免 `hook_replace` 在 `RET` 时跳到签名后的非规范地址。框架不会自动生成 `BR` 远跳；目标超出 `B/BL` 范围时会报错。

## 目标绑定与调用

`bind_func_by_sym` 和 `bind_obj_by_sym` 分别把函数、对象声明绑定到目标二进制里的真实符号。这里的对象包括全局变量和静态对象。普通函数和对象方法统一使用 `bind_func_by_sym`；声明对象方法时，按目标 ABI 将对象指针作为第一个参数。调用已绑定的函数时直接使用 C/C++ 函数调用语法。它们不是指令注入 API；指令注入使用 `patch_asm`。

```cpp
bind_obj_by_sym(u8, cout_obj, "__ZNSt3__14coutE");
bind_func_by_sym(void *, cout_put, (void *, char), "__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE3putEc");
bind_func_by_sym(void *, cout_flush, (void *), "__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE5flushEv");

extern "C" int hook_func(int a, int b) {
    const char *out = "hook ！！\n";
    for (u64 i = 0; out[i]; ++i) {
        cout_put(&cout_obj, out[i]);
    }
    cout_flush(&cout_obj);
    return a + b;
}
```

目标内部函数只有固定虚拟地址时，优先用 `bind_func_by_addr` 声明。它让编译器生成普通函数调用 relocation，patch 阶段会把目标解析为固定 VA，最终通常是相对 `BL`，尾调用可能优化成相对 `B`：

```cpp
bind_func_by_addr(int, internal_add, (int, int), 0x100012340);

static int call_internal(int a, int b) {
    return internal_add(a, b);
}
```

访问固定数据地址（全局变量、typeinfo 等）用 `resolve_addr`。它生成 ADRP+PAGEOFF12 relocation，patch 阶段解析为绝对 VMA，运行时 PC-relative 访问，天然抗 ASLR：

```cpp
#define kAutoplayState 0x1014ED000

static AutoplayState *state() {
    return (AutoplayState *)resolve_addr(kAutoplayState);
}
```

## C++ 使用范围

插件按 C++17 编译，并关闭异常、RTTI 和线程安全静态初始化。推荐使用简单类型和普通函数。避免依赖异常、完整 libc++ 容器和复杂全局构造。

## 构建配置

命令行工具使用 C++17 和 CMake 3.24+ 构建，Mach-O/ELF 的解析与写回能力已内置，不需要下载或安装第三方二进制库。所有平台只需要 CMake、一个 C++17 编译器和 LLVM/Clang；Clang 负责把 `.cpp` 插件和注入汇编编译成 AArch64 中间对象。

编辑项目根目录的 `armcave.conf`：

```text
input = binaries/bin
output = binaries/bin.patched
plugins = plugins
build_dir = build
# plugin_whitelist = arc_autoplay.cpp
# plugin_blacklist = arc_test.cpp
```

构建脚本只读取项目根目录的 `armcave.conf`，不接受命令行参数或环境变量覆盖。`input`、`output`、`plugins` 和 `build_dir` 为必填项；插件白名单和黑名单可以省略。

然后用对应平台的构建脚本：

| 文件 | 平台 |
|---|---|
| `build.sh` | Linux / macOS |
| `build.bat` | Windows |

脚本会根据 `build_dir` 自动配置并编译 `armcave`，然后按同一份配置执行 patch。

### macOS

确认系统已有 `clang` 和 `clang++`，然后安装 CMake：

```bash
clang --version
clang++ --version
brew install cmake
./build.sh
```

### Linux

安装 CMake 和 Clang 后运行构建脚本。例如 Debian/Ubuntu：

```bash
sudo apt install cmake clang
./build.sh
```

### Windows

安装 MSVC 构建工具、CMake 和 LLVM，然后运行构建脚本：

```powershell
winget install Kitware.CMake LLVM.LLVM
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
.\build.bat
```

## TODO

- [x] 支持 Apple AArch64 Mach-O 插件注入。
- [x] 支持 Android AArch64 ELF 插件注入。
- [ ] 替换自研二进制解析器：评估并迁移至 LIEF 或 LLVM 后端，增强对加壳、SHT 压缩和异常段结构的兼容性，避免解析失败直接中止。
- [ ] 实现远跳转 trampoline：当 AArch64 `B/BL` 超出正负 128 MiB 范围时，自动生成间接绝对跳转序列。
- [x] 解决多插件符号冲突：插件独立编译、使用独立段名和符号映射，支持不同插件声明同名 `replacement` / `init` 函数。
- [x] 增强跨平台构建脚本：检测 CMake、Clang/LLVM 和 MSVC 环境，并统一从 `armcave.conf` 读取构建与 patch 配置。
- [ ] 补充性能基准测试：测量 Hook 前后延迟，并与 Frida Stalker、Dobby、E9Patch 等工具进行可复现的横向对比。
- [ ] 增强错误日志与诊断信息：输出结构化失败原因、地址、重定位类型和上下文，替代笼统的 `SKIP` / `errors` 提示。
- [ ] 消除固定 VMA 的版本绑定：组合动态符号、PLT/GOT、字节签名、调用图锚点和用户规则，升级目标二进制后优先自动重定位。
- [ ] 改善隐藏或移除符号的定位：在没有符号元数据时使用稳定代码签名和用户规则，减少对 `bind_func_by_addr` / `resolve_addr` 固定地址配置的依赖。
