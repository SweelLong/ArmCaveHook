# ArmCaveHook

中文 | [English](README_EN.md)

<p align="center">
  <img src="https://img.shields.io/badge/Arch-ARM64%20%7C%20AArch64-blue?logo=arm" alt="ARM64">
  <img src="https://img.shields.io/badge/Apple-iOS%20Mach--O-lightgrey?logo=apple" alt="Apple">
    <img src="https://img.shields.io/badge/Android-Android%20ELF-lightgrey?logo=android" alt="Android">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-blue?logo=cplusplus" alt="C++17">
  <img src="https://img.shields.io/badge/Binary-Built--in-orange" alt="Built-in binary support">
</p>

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
    hook(0x100000498, replacement, w0, w1);
}
```

`SEGMENT_NAME` 是插件段名。Mach-O 使用 `__testhook`，ELF 使用 `.testhook`。每个插件只生成一个 segment；框架会先汇总该插件全部 hook dispatcher、寄存器 wrapper、编译后代码、常量数据和重定位所需空间，再创建最终大小的 segment。插件代码和常量只存放一份，多个 `hook`/`pre_hook` 共用它们。

## 标准 API

| API | 作用 |
|---|---|
| `hook(addr, handler, ...)` | 函数替代 hook，从目标地址跳到 handler。 |
| `pre_hook(addr, handler, ...)` | 前置 hook，先调用 handler，再执行被覆盖的原指令并回到原函数。 |
| `inject_asm(addr, "instruction")` | 汇编 AArch64 指令并写入虚拟地址。 |
| `inject_hex(addr, "hex")` | 写入十六进制机器码。 |
| `patch_imm12(addr, expected)` | 当前指令等于 `expected` 时清掉 ADD/SUB imm12 位。 |
| `target_fn(ret, name, args, symbol)` | 按符号名声明目标二进制里的函数。 |
| `target_va_fn(ret, name, args, addr)` | 按固定虚拟地址声明目标二进制里的函数，patch 后生成相对 `BL/B` relocation。 |
| `target_obj(name, symbol)` | 按符号名声明目标二进制里的通用对象。 |
| `target_obj(type, name, symbol)` | 按符号名声明目标二进制里的强类型对象。 |
| `target_obj_fn(name, symbol, ...)` | 按符号名声明目标对象函数。 |
| `target_obj_call(fn, obj, ...)` | 调用目标对象函数。 |
| `target_addr(va)` | 将 VMA 转为运行时地址（ADRP+PAGEOFF12，抗 ASLR），用于数据地址引用。 |
| `target_call(ret, addr, args, ...)` | 按虚拟地址快速调用目标内部函数（BR26 PC-relative BL，抗 ASLR）。 |
| `target_call_offset(ret, offset, args, ...)` | 按文件偏移调用目标内部函数（自动加 `ARMCAVE_BASE`，抗 ASLR）。 |
| `ARMCAVE_BASE` | 目标二进制加载基址，默认 `0x100000000`。 |
| `vt_call(obj, idx, arg)` | 调用对象虚表第 `idx` 项，返回 `string`（ARM64 sret x8 自动处理）。 |
| `read<T>(addr)` / `write<T>(addr, value)` | 读写目标进程内存。 |
| `vcall(obj, offset)` | 读取对象虚表中指定偏移的函数指针。 |
| `object_typeinfo(obj)` | 读取 Itanium C++ ABI vtable 前的 typeinfo 指针。 |
| `logf(fmt, ...)` | 简单日志输出。 |
| `string` | 封装目标 libc++ `std::string` 布局（sizeof=24，SSO up to 22 chars），自动适配 Apple libc++ 与 Android NDK libc++，纯自实现 `assign`/`append`。 |
| `vector<T>` | 动态 C++ 容器，使用目标堆（`malloc`/`free`），支持扩容。 |
| `u8/u16/u32/u64/i8/i16/i32/i64/addr_t` | 基础类型别名。 |

`hook` 和 `pre_hook` 后面的寄存器参数可选。传入寄存器后，框架会生成 wrapper，把这些寄存器移动到标准 AArch64 调用参数寄存器。

`hook` 和 `pre_hook` 都是静态写跳转到 code cave，但控制流不同：

```text
hook:     target -> handler -> ret
pre_hook: target -> handler -> 原始被覆盖指令 -> target + 4
```

因此 `hook` 适合替换一个函数或入口点，`pre_hook` 适合在原逻辑前插入一段代码并继续执行原函数。`pre_hook` 的 hook 点应放在目标函数已经把关键状态保存到 callee-saved 寄存器之后；handler 应遵守 AArch64 调用约定，不要依赖 caller-saved 寄存器在返回后保持不变。

## 指令注入

```cpp
#define SEGMENT_NAME patchseg

void init(void) {
    inject_asm(0x100500000, "NOP");
    inject_asm(0x100500004, "MOV W0, #1; RET");
    inject_hex(0x100500010, "1f2003d5");
}
```

`inject_asm` 用来写 AArch64 汇编文本，`inject_hex` 用来写已经确认好的机器码。

默认跳转使用 AArch64 `B` 指令。`B` 是 26-bit 相对跳转，按 4 字节指令对齐计算，范围是当前位置前后 128 MiB。Mach-O hook cave 入口会先执行 `XPACLRI` 再保存 `x29/x30`，用于清理带 PAC 签名位的返回地址，避免 detour hook 在 `RET` 时跳到签名后的非规范地址。框架不会在普通 hook 路径中自动生成 `BR` 远跳；目标超出 `B/BL` 范围时会报错。

## 目标函数调用

`target_fn` 用来把插件函数声明绑定到目标二进制里的真实符号。它不是指令注入 API；指令注入使用 `inject_asm`。

```cpp
target_obj(cout_obj, "__ZNSt3__14coutE");
target_obj_fn(cout_put, "__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE3putEc", char);
target_obj_fn(cout_flush, "__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE5flushEv");

extern "C" int hook_func(int a, int b) {
    string out = "hook ！！\n";
    for (u64 i = 0; i < out.size(); ++i) {
        target_obj_call(cout_put, cout_obj, out[i]);
    }
    target_obj_call(cout_flush, cout_obj);
    return a + b;
}
```

目标内部函数只有固定虚拟地址时，优先用 `target_va_fn` 声明。它让编译器生成普通函数调用 relocation，patch 阶段会把目标解析为固定 VA，最终通常是相对 `BL`，尾调用可能优化成相对 `B`：

```cpp
target_va_fn(int, internal_add, (int, int), 0x100012340);

static int call_internal(int a, int b) {
    return internal_add(a, b);
}
```

`ARMCAVE_BASE` 默认为 `0x100000000`（标准 arm64 Mach-O），可在插件中 `#define ARMCAVE_BASE` 覆盖。`target_call` 和 `target_call_offset` 的使用见下文。

## 虚表调用

通过对象的虚表指针可以调用游戏内部方法。`vt_call` 封装了 ARM64 sret（x8）调用约定，直接返回 `string`：

```cpp
string content = vt_call(file_manager, 5, path_string);
const char *data = content.c_str();  // SSO/long 模式自动处理
```

需要先调用目标内部函数再拿对象做虚表调用时，也优先把目标函数声明成 `target_va_fn`。这样插件代码不需要 `_dyld_get_image_header(0)`、`dladdr` 或平台私有 loader API 来计算模块基址；地址解析交给 patch 阶段处理，插件源码更容易跨 Mach-O/ELF 等格式复用。访问固定数据地址时使用 `target_addr`：

```cpp
#include "armcave.h"
#define SEGMENT_NAME arcrating

target_va_fn(void *, get_file_manager, (void), 0x100DC491C);

static void read_ratinglist() {
    void *fm = get_file_manager();
    if (!fm)
        return;

    string po = vt_call(fm, 5, path_string);
    // 解析 po 内容...
}
```

访问固定数据地址（全局变量、typeinfo 等）用 `target_addr`。它生成 ADRP+PAGEOFF12 relocation，patch 阶段解析为绝对 VMA，运行时 PC-relative 访问，天然抗 ASLR：

```cpp
#define kAutoplayState 0x1014ED000

static AutoplayState *state() {
    return (AutoplayState *)target_addr(kAutoplayState);
}
```

`target_call` 和 `target_call_offset` 现在也走 BR26 PC-relative BL，不再通过函数指针间接调用，同样抗 ASLR：

```cpp
static int call_internal(int a, int b) {
    return target_call(int, 0x100012340, (int, int), a, b);
}

// 文件偏移方式，框架自动加 ARMCAVE_BASE
target_call_offset(void *, 0xd4785c, (void *, const char *), buf, path);
```

## C++ 使用范围

插件按 C++17 编译，并关闭异常、RTTI 和线程安全静态初始化。推荐使用简单类型、普通函数及框架内置的 `vector<T>` 和 `string`（使用目标堆/malloc，无需额外配置）。避免依赖异常、完整 libc++ 容器和复杂全局构造。

## 构建配置

命令行工具使用 C++17 和 CMake 3.24+ 构建，Mach-O/ELF 的解析与写回能力已内置，不需要下载或安装第三方二进制库。所有平台只需要 CMake、一个 C++17 编译器和 LLVM/Clang；Clang 负责把 `.cpp` 插件和注入汇编编译成 AArch64 中间对象。

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

编辑项目根目录的 `armcave.conf`：

```text
input = binaries/bin
output = binaries/bin.patched
# plugin_whitelist = arc_rating.cpp, arc_autoplay.cpp
# plugin_blacklist = arc_test.cpp
```

然后用对应平台的构建脚本：

| 文件 | 平台 |
|---|---|
| `build.sh` | Linux / macOS |
| `build.bat` | Windows |

脚本会自动配置并编译 `build/armcave`。

## 目标二进制支持状态

- [x] iOS AArch64 Mach-O 插件注入
- [x] Android AArch64 ELF 插件注入

当前限制：

- 固定 VMA 仅适用于对应目标版本，升级目标二进制后需要重新定位地址。
- 被目标 ELF 完全隐藏或移除的符号仍需使用 `target_va_fn` / `target_addr` 固定地址定位。
