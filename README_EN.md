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

[中文](README.md) | English

ArmCaveHook is an AArch64 static hooking framework. Plugins are written in `.cpp`, and the framework compiles them into code cave sections, then patches the target binary according to the plugin's entry declarations.

## Quick Start

```bash
git clone --recursive https://github.com/SweelLong/ArmCaveHook.git
cd ArmCaveHook
./build.sh
```

Plugins and binaries are managed by the [`ArmCaveHook-Arcplugins`](https://github.com/SweelLong/ArmCaveHook-Arcplugins) submodule.
You can create your own plugin repository by modifying `input`, `output`, and `plugins` in `armcave.conf`.

## Plugin Specification

The plugin must include the standard header at the top:

```cpp
#include "armcave.h"
```

The top level may only contain includes, optional `#define SEGMENT_NAME`, type declarations, function declarations, global variables, function definitions, and `init(void)`. Aside from variables and declarations, do not place executable logic outside functions. All modifications to the binary go inside `init(void)`.

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

`SEGMENT_NAME` is the plugin segment name. Mach-O uses `__testhook`, while ELF uses `.testhook`. Each plugin creates exactly one segment. The framework first totals the space required by all hook_replace dispatchers, register wrappers, compiled code, constant data, and relocations, then creates the final-sized segment. Plugin code and constants are stored once and shared by all `hook_replace`/`hook_detour` actions.

## Standard API

| API | Description |
|---|---|
| `hook_replace(addr, handler, ...)` | Replaces the target function with handler; returning from handler leaves the original function. Optional trailing register names such as `x0, x1` are supported. |
| `hook_detour(addr, handler, ...)` | Calls handler at the address, then executes the overwritten original instructions and returns to the original function; trailing register names such as `x20` are optional. |
| `patch_asm(addr, "instruction"[, "expected"])` | Assemble and write AArch64 instructions; with `expected`, write only when an equally-sized original instruction sequence matches. |
| `bind_obj_by_sym(type, name, symbol)` | Binds an object declaration, including a global variable or static object, to a symbol in the target binary. |
| `bind_func_by_sym(ret, name, args, symbol)` | Binds a function declaration to a symbol in the target binary. |
| `bind_func_by_addr(ret, name, args, addr)` | Binds a function declaration to a fixed virtual address, generating a relative `BL/B` relocation after patching. |
| `resolve_addr(va)` | Converts VMA to runtime address (ADRP+PAGEOFF12, ASLR-resistant) for data address references. |
| `read_mem<T>(addr)` / `write_mem<T>(addr, value)` | Read/write target process memory. |
| `resolve_vfunc(obj, offset)` | Reads a function pointer at a given offset from an object's vtable. |
| `read_typeinfo(obj)` | Reads the typeinfo pointer before the vtable in Itanium C++ ABI. |
| `armcave_itoa(buf, value)` | Write an integer to a buffer and return its character count. |
| `armcave_json_value(json, key, out, size)` | Read a string value from a JSON object using an integer key. |
| `armcave_apple_string_make(text)` | Build an Apple 24-byte short-string argument. |
| `armcave_apple_string_data(value)` | Get the actual character address from an Apple string. |
| `armcave_apple_file_manager_get(manager, path)` | Call the Apple file manager resource-reading method. |
| `logf(fmt, ...)` | Simple logging output; trailing arguments correspond to the format string, such as `logf("value=%d", value)`. |
| `u8/u16/u32/u64/i8/i16/i32/i64/addr_t` | Basic type aliases. |

When hook registers are provided, the framework generates a wrapper that moves them to the standard AArch64 calling convention argument registers.

Both `hook_replace` and `hook_detour` statically write jumps to a code cave, but differ in control flow:

```text
hook_replace: target -> cave -> handler -> RET
hook_detour:  target -> cave -> handler -> original overwritten instruction -> target + 4
```

Thus `hook_replace` is suitable for completely replacing a function or entry point, while `hook_detour` inserts handling into the original flow and then continues it. The hook point for `hook_detour` should be placed after the target function has saved critical state to callee-saved registers; the handler should follow the AArch64 calling convention and not rely on caller-saved registers remaining unchanged after return.

## Instruction Injection

```cpp
#define SEGMENT_NAME patchseg

void init(void) {
    patch_asm(0x100500000, "NOP");
    patch_asm(0x100500004, "MOV W0, #1; RET", "NOP; NOP");
}
```

`patch_asm` writes AArch64 assembly text. Use the form with `expected` when the patch should be guarded against version changes.

Default jumps use the AArch64 `B` instruction. `B` is a 26-bit relative jump, aligned to 4-byte instruction boundaries, with a range of 128 MiB forward and backward from the current position. The Mach-O hook cave entry executes `XPACLRI` before saving `x29/x30` to clear PAC-signed return addresses, preventing `hook_replace` from jumping to a non-canonical signed address on `RET`. The framework does not automatically generate `BR` far jumps; it reports an error if the target is out of `B/BL` range.

## Target Binding And Calls

`bind_func_by_sym` and `bind_obj_by_sym` bind function and object declarations to real symbols in the target binary. Objects include global variables and static objects. Regular functions and object methods both use `bind_func_by_sym`; for an object method, declare the object pointer as the first parameter according to the target ABI. Call bound functions directly with normal C/C++ function-call syntax. These are not instruction injection APIs; for that, use `patch_asm`.

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

When a target internal function only has a fixed virtual address, prefer `bind_func_by_addr` to declare it. This lets the compiler generate a normal function call relocation; the patching stage resolves it to a fixed VA, resulting in a relative `BL` (or relative `B` for tail calls):

```cpp
bind_func_by_addr(int, internal_add, (int, int), 0x100012340);

static int call_internal(int a, int b) {
    return internal_add(a, b);
}
```

Use `resolve_addr` to access fixed data addresses (global variables, typeinfo, etc.). It generates an ADRP+PAGEOFF12 relocation, resolved to an absolute VMA during patching, and accessed PC-relatively at runtime, inherently ASLR-resistant:

```cpp
#define kAutoplayState 0x1014ED000

static AutoplayState *state() {
    return (AutoplayState *)resolve_addr(kAutoplayState);
}
```

## C++ Usage Scope

Plugins are compiled as C++17 with exceptions, RTTI, and thread-safe static initialization disabled. Simple types and plain functions are recommended. Avoid depending on exceptions, full libc++ containers, and complex global constructors.

## Build Configuration

The command-line tool is built with C++17 and CMake 3.24+. Mach-O/ELF parsing and rewriting are built in, so no third-party binary library is downloaded or installed. Every host only needs CMake, a C++17 compiler, and LLVM/Clang. Clang compiles `.cpp` plugins and assembles AArch64 instructions into the intermediate objects consumed by the patcher.

Edit `armcave.conf` in the project root:

```text
input = binaries/bin
output = binaries/bin.patched
plugins = plugins
build_dir = build
# plugin_whitelist = arc_autoplay.cpp
# plugin_blacklist = arc_test.cpp
```

The build scripts read only `armcave.conf` from the project root. They do not accept command-line arguments or environment-variable overrides. `input`, `output`, `plugins`, and `build_dir` are required; the plugin whitelist and blacklist may be omitted.

Then use the build script for your platform:

| File | Platform |
|---|---|
| `build.sh` | Linux / macOS |
| `build.bat` | Windows |

The scripts configure and build `armcave` under `build_dir`, then run the patch using the same configuration.

### macOS

Confirm that `clang` and `clang++` are available, then install CMake:

```bash
clang --version
clang++ --version
brew install cmake
./build.sh
```

### Linux

Install CMake and Clang, then run the build script. For example on Debian/Ubuntu:

```bash
sudo apt install cmake clang
./build.sh
```

### Windows

Install the MSVC build tools, CMake, and LLVM, then run the build script:

```powershell
winget install Kitware.CMake LLVM.LLVM
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
.\build.bat
```

## TODO

- [x] Support Apple AArch64 Mach-O plugin injection.
- [x] Support Android AArch64 ELF plugin injection.
- [ ] Replace the in-house binary parser: evaluate and migrate to a LIEF or LLVM backend for packed binaries, compressed SHT data, and unusual segment layouts without aborting on parser failures.
- [ ] Implement far-jump trampolines: emit an indirect absolute jump when an AArch64 `B/BL` target is outside the plus or minus 128 MiB range.
- [x] Isolate symbols across plugins: plugins are compiled independently and use separate segment names and symbol maps, allowing duplicate `replacement` / `init` names.
- [x] Harden cross-platform build scripts: detect CMake, Clang/LLVM, and MSVC, and consistently read build and patch settings from `armcave.conf`.
- [ ] Add reproducible performance benchmarks for Hook overhead and comparisons with Frida Stalker, Dobby, and E9Patch.
- [ ] Improve diagnostics with structured failure reasons, addresses, relocation types, and context instead of generic `SKIP` / `errors` messages.
- [ ] Remove fixed-VMA version coupling using dynamic symbols, PLT/GOT, byte signatures, call-graph anchors, and user rules to relocate automatically after target upgrades.
- [ ] Improve location of hidden or removed symbols with stable code signatures and user rules, reducing reliance on fixed `bind_func_by_addr` / `resolve_addr` configuration.
