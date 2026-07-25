# ArmCaveHook

[中文](README.md) | English

<p align="center">
  <img src="https://img.shields.io/badge/Arch-ARM64%20%7C%20AArch64-blue?logo=arm" alt="ARM64">
  <img src="https://img.shields.io/badge/Apple-iOS%20Mach--O-lightgrey?logo=apple" alt="Apple">
  <img src="https://img.shields.io/badge/Android-Android%20ELF-lightgrey?logo=android" alt="Android">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-blue?logo=cplusplus" alt="C++17">
  <img src="https://img.shields.io/badge/Binary-Built--in-orange" alt="Built-in binary support">
</p>

ArmCaveHook is an AArch64 static hooking framework. Plugins are written in `.cpp`, and the framework compiles them into code cave sections, then patches the target binary according to the plugin's entry declarations.

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
    hook(0x100000498, replacement, w0, w1);
}
```

`SEGMENT_NAME` is the plugin segment name. Mach-O uses `__testhook`, while ELF uses `.testhook`. Each plugin creates exactly one segment. The framework first totals the space required by all hook dispatchers, register wrappers, compiled code, constant data, and relocations, then creates the final-sized segment. Plugin code and constants are stored once and shared by all `hook`/`pre_hook` actions.

## Standard API

| API | Description |
|---|---|
| `hook(addr, handler, ...)` | Function replacement hook, jumps from target address to handler. |
| `pre_hook(addr, handler, ...)` | Pre-hook, calls handler first, then executes the overwritten original instructions and returns to the original function. |
| `inject_asm(addr, "instruction")` | Assembles an AArch64 instruction and writes it to a virtual address. |
| `inject_hex(addr, "hex")` | Writes hexadecimal machine code. |
| `patch_imm12(addr, expected)` | Clears the ADD/SUB imm12 bits when the current instruction equals `expected`. |
| `target_fn(ret, name, args, symbol)` | Declares a function in the target binary by symbol name. |
| `target_va_fn(ret, name, args, addr)` | Declares a function in the target binary by fixed virtual address, generates relative `BL/B` relocation after patching. |
| `target_obj(name, symbol)` | Declares a generic object in the target binary by symbol name. |
| `target_obj(type, name, symbol)` | Declares a strongly-typed object in the target binary by symbol name. |
| `target_obj_fn(name, symbol, ...)` | Declares a target object function by symbol name. |
| `target_obj_call(fn, obj, ...)` | Calls a target object function. |
| `target_addr(va)` | Converts VMA to runtime address (ADRP+PAGEOFF12, ASLR-resistant) for data address references. |
| `target_call(ret, addr, args, ...)` | Quickly calls a target internal function by virtual address (BR26 PC-relative BL, ASLR-resistant). |
| `target_call_offset(ret, offset, args, ...)` | Calls a target internal function by file offset (auto-adds `ARMCAVE_BASE`, ASLR-resistant). |
| `ARMCAVE_BASE` | Target binary load address, defaults to `0x100000000`. |
| `read<T>(addr)` / `write<T>(addr, value)` | Read/write target process memory. |
| `vcall(obj, offset)` | Reads a function pointer at a given offset from an object's vtable. |
| `object_typeinfo(obj)` | Reads the typeinfo pointer before the vtable in Itanium C++ ABI. |
| `logf(fmt, ...)` | Simple logging output. |
| `u8/u16/u32/u64/i8/i16/i32/i64/addr_t` | Basic type aliases. |

The register parameters after `hook` and `pre_hook` are optional. When registers are provided, the framework generates a wrapper that moves those registers to the standard AArch64 calling convention argument registers.

Both `hook` and `pre_hook` statically write jumps to a code cave, but differ in control flow:

```text
hook:     target -> handler -> ret
pre_hook: target -> handler -> original overwritten instruction -> target + 4
```

Thus `hook` is suitable for replacing a function or entry point, while `pre_hook` is suitable for inserting code before the original logic and continuing execution. The hook point for `pre_hook` should be placed after the target function has saved critical state to callee-saved registers; the handler should follow the AArch64 calling convention and not rely on caller-saved registers remaining unchanged after return.

## Instruction Injection

```cpp
#define SEGMENT_NAME patchseg

void init(void) {
    inject_asm(0x100500000, "NOP");
    inject_asm(0x100500004, "MOV W0, #1; RET");
    inject_hex(0x100500010, "1f2003d5");
}
```

`inject_asm` writes AArch64 assembly text, `inject_hex` writes pre-verified machine code.

Default jumps use the AArch64 `B` instruction. `B` is a 26-bit relative jump, aligned to 4-byte instruction boundaries, with a range of 128 MiB forward and backward from the current position. The Mach-O hook cave entry executes `XPACLRI` before saving `x29/x30` to clear PAC-signed return addresses, preventing detour hooks from jumping to non-canonical signed addresses on `RET`. The framework does not automatically generate `BR` far jumps in normal hook paths; it will report an error if the target is out of `B/BL` range.

## Target Function Calls

`target_fn` binds a plugin function declaration to a real symbol in the target binary. It is not an instruction injection API; for that, use `inject_asm`.

```cpp
target_obj(cout_obj, "__ZNSt3__14coutE");
target_obj_fn(cout_put, "__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE3putEc", char);
target_obj_fn(cout_flush, "__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE5flushEv");

extern "C" int hook_func(int a, int b) {
    const char *out = "hook ！！\n";
    for (u64 i = 0; out[i]; ++i) {
        target_obj_call(cout_put, cout_obj, out[i]);
    }
    target_obj_call(cout_flush, cout_obj);
    return a + b;
}
```

When a target internal function only has a fixed virtual address, prefer `target_va_fn` to declare it. This lets the compiler generate a normal function call relocation; the patching stage resolves it to a fixed VA, resulting in a relative `BL` (or relative `B` for tail calls):

```cpp
target_va_fn(int, internal_add, (int, int), 0x100012340);

static int call_internal(int a, int b) {
    return internal_add(a, b);
}
```

`ARMCAVE_BASE` defaults to `0x100000000` (standard arm64 Mach-O), and can be overridden with `#define ARMCAVE_BASE` in the plugin. Usage of `target_call` and `target_call_offset` is shown below.

Use `target_addr` to access fixed data addresses (global variables, typeinfo, etc.). It generates an ADRP+PAGEOFF12 relocation, resolved to an absolute VMA during patching, and accessed PC-relatively at runtime, inherently ASLR-resistant:

```cpp
#define kAutoplayState 0x1014ED000

static AutoplayState *state() {
    return (AutoplayState *)target_addr(kAutoplayState);
}
```

`target_call` and `target_call_offset` now also use BR26 PC-relative BL instead of indirect function pointer calls, also ASLR-resistant:

```cpp
static int call_internal(int a, int b) {
    return target_call(int, 0x100012340, (int, int), a, b);
}

// file offset mode, framework auto-adds ARMCAVE_BASE
target_call_offset(void *, 0xd4785c, (void *, const char *), buf, path);
```

## C++ Usage Scope

Plugins are compiled as C++17 with exceptions, RTTI, and thread-safe static initialization disabled. Simple types and plain functions are recommended. Avoid depending on exceptions, full libc++ containers, and complex global constructors.

## Build Configuration

The command-line tool is built with C++17 and CMake 3.24+. Mach-O/ELF parsing and rewriting are built in, so no third-party binary library is downloaded or installed. Every host only needs CMake, a C++17 compiler, and LLVM/Clang. Clang compiles `.cpp` plugins and assembles AArch64 instructions into the intermediate objects consumed by the patcher.

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

Edit `armcave.conf` in the project root:

```text
input = binaries/bin
output = binaries/bin.patched
# plugin_whitelist = arc_autoplay.cpp
# plugin_blacklist = arc_test.cpp
```

Arguments may also be supplied directly. Command-line options override `ARMCAVE_*` environment variables, which override `armcave.conf`:

```bash
./build.sh --input binaries/libcocos2dcpp.so \
  --output binaries/libcocos2dcpp.patched.so \
  --plugin-whitelist arc_rating_so.cpp
```

Then use the build script for your platform:

| File | Platform |
|---|---|
| `build.sh` | Linux / macOS |
| `build.bat` | Windows |

The scripts configure and build `build/armcave` automatically.

## TODO

- [x] Support iOS AArch64 Mach-O plugin injection.
- [x] Support Android AArch64 ELF plugin injection.
- [ ] Replace the in-house binary parser: evaluate and migrate to a LIEF or LLVM backend for packed binaries, compressed SHT data, and unusual segment layouts without aborting on parser failures.
- [ ] Implement far-jump trampolines: emit an indirect absolute jump when an AArch64 `B/BL` target is outside the plus or minus 128 MiB range.
- [x] Isolate symbols across plugins: plugins are compiled independently and use separate segment names and symbol maps, allowing duplicate `replacement` / `init` names.
- [x] Harden cross-platform build scripts: detect CMake, Clang/LLVM, and MSVC, and allow command-line arguments or environment variables to override `armcave.conf` for CI/CD and batch use.
- [ ] Add reproducible performance benchmarks for Hook overhead and comparisons with Frida Stalker, Dobby, and E9Patch.
- [ ] Improve diagnostics with structured failure reasons, addresses, relocation types, and context instead of generic `SKIP` / `errors` messages.
- [ ] Remove fixed-VMA version coupling using dynamic symbols, PLT/GOT, byte signatures, call-graph anchors, and user rules to relocate automatically after target upgrades.
- [ ] Improve location of hidden or removed symbols with stable code signatures and user rules, reducing reliance on fixed `target_va_fn` / `target_addr` configuration.
