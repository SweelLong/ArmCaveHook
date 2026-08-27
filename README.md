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

ArmCaveHook is an AArch64 static binary patch framework. It compiles C++ plugins into independent code and data segments, analyzes original instructions, generates trampolines, resolves relocations, and writes the result back to 64-bit Mach-O or ELF binaries.

The only supported target architecture is ARM64/AArch64. Apple targets use 64-bit Mach-O and Android targets use 64-bit ELF. x86, 32-bit ARM, and other target architectures are not supported.

## Quick Start

```bash
git clone --recursive https://github.com/SweelLong/ArmCaveHook.git
cd ArmCaveHook
./build.sh
```

Before running the script, set the desired profile to `enable = true` in `armcave.conf`.

The command-line tool can also be built directly:

```bash
cmake -S . -B build
cmake --build build -j2
```

## Architecture

```text
Plugin .cpp
    |
    v
Clang AArch64 object
    |
    +-- metadata parser
    +-- symbol and PLT/GOT resolver
    +-- byte signature resolver
    +-- AArch64 decoder and CFG analyzer
    +-- function IR and function discovery
    +-- instruction relocator
    +-- Mach-O chained fixups and Apple metadata
    +-- patch.toml script layer
    +-- code/data segment planner
    +-- Mach-O or ELF writer
    v
Patched ARM64 binary
```

The framework keeps its built-in Mach-O/ELF parser and writer. It does not require LIEF or
LLVM as a binary parsing backend, and the current plugin format and ARM64 patch pipeline remain
unchanged.

## Minimal Plugin

A plugin only needs to include `armcave.h`, define a handler, and declare its patch in
`init(void)`:

```cpp
#include "armcave.h"

extern "C" int replacement(int value) {
    return value + 1;
}

extern "C" void init(void) {
    hook_replace(0x100000498, replacement, w0);
}
```

The framework handles independent plugin compilation, symbol mapping, segment-name conflicts,
handler wrappers, code/data capacity planning, trampolines, and relocation. Plugins do not
need to write cave code, save registers, return to the original function, or relocate copied
instructions manually.

## Hook API

| API | Description |
|---|---|
| `hook_replace(addr, handler, ...)` | Calls the handler and returns without executing the overwritten original logic. |
| `hook_detour(addr, handler, ...)` | Calls the handler, relocates the overwritten original instructions, and resumes the original function. |
| `hook_replace_signature(pattern, handler, ...)` | Finds one unique AArch64 byte signature and replaces the matched location. |
| `hook_detour_signature(pattern, handler, ...)` | Finds one unique AArch64 byte signature and detours the matched location. |
| `hook_replace_symbol(symbol, handler, ...)` | Replaces one unique target symbol or demangled C++ function. |
| `hook_detour_symbol(symbol, handler, ...)` | Detours one unique target symbol or demangled C++ function. |
| `replace_function(match(symbol), handler, ...)` | Function-level DSL spelling of `hook_replace_symbol`. |
| `detour_function(match(symbol), handler, ...)` | Function-level DSL spelling of `hook_detour_symbol`. |
| `hook_objc_method(class_name, selector, handler, ...)` | Resolves Apple Objective-C metadata and replaces the method IMP. |
| `hook_detour_objc_method(class_name, selector, handler, ...)` | Resolves Apple Objective-C metadata and detours the method IMP. |
| `patch_asm(addr, "...")` | Writes assembled AArch64 instructions. |
| `patch_asm(addr, "...", "expected")` | Writes only when the original instruction sequence matches `expected`. |
| `patch_asm_func(addr, id)` | Replaces the call-site instruction with `BL` to a registered assembly or C++ function. Use `patch_asm` separately to move the call argument into `w0` or `x0`. |
| `new_asm_func_id(id, ["...", "..."])` / `new_asm_func(["...", "..."])` | Registers a pure-assembly function with local labels; reference it with `patch_asm_func`. |
| `new_cpp_func_id(id, handler)` / `new_cpp_func(handler)` | Registers a C++ handler under a stable ID. Source registers are specified at each `patch_asm_func` call. |
| `bind_func_by_sym(ret, name, args, symbol)` | Binds a normal, C++, or imported function symbol. |
| `bind_func_by_addr(ret, name, args, addr)` | Binds a fixed target address and resolves generated `B/BL` relocations. |
| `bind_obj_by_sym(type, name, symbol)` | Binds a global or static target object. |
| `resolve_addr(va)` | Generates a target data-address reference with ADRP/PAGEOFF12 relocation. |
| `read_mem<T>(addr)` / `write_mem<T>(addr, value)` | Reads or writes target memory. |
| `armcave_timer_now_ms(...)` | Reads milliseconds using a target timer layout and fallback policy. |
| `resolve_vfunc(obj, offset)` | Reads a function pointer from an object's vtable. |
| `read_typeinfo(obj)` | Reads Itanium C++ ABI typeinfo. |
| `armcave_string` / `armcave_string_make` / `armcave_string_data` / `armcave_string_size` / `armcave_string_destroy` | Automatically handles the Apple or Android 24-byte string ABI selected for the target. |
| `armcave_json_value(json, key, out, size)` | Reads a string from a JSON object using an integer key. |
| `armcave_json_or_integer(json, key, label, size, fallback)` | Uses a JSON label when available and otherwise formats the integer key. |
| `armcave_json_copy_or_integer(json, key, out, size)` | Copies a JSON label or numeric fallback into a platform-neutral text buffer. |
| `armcave_asset_reader` / `armcave_asset_load` / `armcave_asset_release` | Cross-platform asset lifetime API with platform-owned open/close adapters. |
| `armcave_asset_binary_reader` / `armcave_asset_binary_load` | Reuses binary-asset length checks, chunked reads, allocation, and release. |

Register arguments are copied by a generated wrapper into standard AArch64 argument registers:

```cpp
hook_detour(0x10087038c, on_tick, x20, w21);
```

The handler does not need to know the size of the overwritten window.

## Branches, Far Jumps, and Relocation

The default hook-site transfer is a non-linking AArch64 `B` instruction. A `B` uses a signed
26-bit instruction offset and reaches approximately 128 MiB in either direction.

The framework automatically selects a larger sequence when the destination is farther away:

```text
4 bytes:  B target
12 bytes: ADRP x16, target; ADD x16, x16, #pageoff; BR x16
20 bytes: MOVZ/MOVK x16, absolute; BR x16
```

For a linking call, the same selection uses `BL` within range and `BLR` in the indirect forms.
The hook window automatically expands to 4, 12, or 20 bytes and fills unused bytes with NOPs.
When a detour window expands, the original instructions are relocated before execution resumes.

The AArch64 relocator supports:

- `B` and `BL`
- `B.cond`
- `CBZ` and `CBNZ`
- `TBZ` and `TBNZ`
- `ADR` and `ADRP`
- Common literal `LDR` forms
- Basic-block internal target remapping

For copied plugin code, an out-of-range `ARM64_RELOC_BRANCH26` automatically receives a local
veneer. Plugin text and data capacity are reserved for the worst-case veneer size before the
segments are created.

On Mach-O hook caves, the entry sequence also handles PAC return-address cleanup before saving
the frame when required by the target.

## Version-Independent Location

The framework does not guess a new address from an old fixed address. A plugin must declare how
each target location is found:

| Locator | Use case | Version migration |
|---|---|---|
| Fixed address | Known version, no symbol for an internal function | Same layout only |
| Target symbol | Exported, C++, or imported function | Usually cross-version |
| Objective-C class + selector | Apple Objective-C method | Usually cross-version |
| Unique byte signature | AArch64 entry without a stable symbol | Survives code layout changes |
| Expected instruction | Guard for a fixed-address patch | Validation only, no migration |

### Byte Signatures

When no stable symbol is available, use `hook_replace_signature` or `hook_detour_signature`:

```cpp
extern "C" void on_tick(void *object) {
    read_mem<void *>(reinterpret_cast<addr_t>(object));
}

extern "C" void init(void) {
    hook_detour_signature(
        "FD 7B BF A9 ?? ?? ?? ?? FD 03 00 91 ?? ?? ?? ??",
        on_tick, x0);
}
```

Signature bytes may be separated by spaces, commas, or semicolons. `?`, `??`, and `*` are
wildcards. AArch64 `BL`, `B`, `ADR`, `ADRP`, and literal-load instructions contain
version-dependent PC-relative immediates. Those bytes should normally be wildcarded while stable
function-prologue, register-operation, and return-path bytes remain in the signature.

Signatures scan executable sections and must produce exactly one match. Zero matches mean the
signature is stale; multiple matches mean it is not specific enough. Both cases stop the patch and
never choose an arbitrary location.

### Symbol Location

Prefer symbol APIs when the target keeps symbols:

```cpp
bind_func_by_sym(void, target_update, (void *), "_ZN6Player6updateEv");

extern "C" void replacement(void *player) {
    target_update(player);
}

extern "C" void init(void) {
    hook_replace_symbol("_ZN6Player6updateEv", replacement, x0);
}
```

`bind_func_by_addr`, `hook_replace(addr, ...)`, and `hook_detour(addr, ...)` remain fixed-address
APIs. They do not search for a new-version location. Target-function addresses, object field
offsets, and ABI changes in a plugin must also be handled separately.

### Pure Assembly Functions

`new_asm_func_id` registers a pure assembly body under a stable numeric ID. Labels are
local to that function and can be used by `B`, `BL`, and conditional branches. It does
not return an address; `patch_asm_func` resolves the generated function address after the
plugin segment has been laid out:

```cpp
extern "C" void init(void) {
    new_asm_func_id(1, [
        "loop:",
        "mov w0, #1",
        "b loop"
    ]);
}
```

For reuse at multiple call sites, give the assembly function a stable numeric ID and use
`patch_asm_func` at each site. The function patch replaces one instruction with `BL`. It does
not move values between registers; use a separate `patch_asm` at the call site when the target
function needs a value in a specific register.

```cpp
enum { kFunction = 1 };

extern "C" void init(void) {
    new_asm_func_id(kFunction, [
        "cmp w0, #10",
        "b.hs enabled",
        "mov w0, #0",
        "ret",
        "enabled:",
        "mov w0, #1",
        "ret"
    ]);
    patch_asm(0x100000100, "mov w0, w8");
    patch_asm_func(0x100000104, kFunction);
}
```

### C++ Functions

Use `new_cpp_func_id` when the function body is clearer in C++. The handler follows the normal
C++ AArch64 ABI: its first integer or pointer argument is read from `x0`/`w0`, and its return
value is produced in `x0`. `patch_asm_func` only emits the `BL`; prepare `w0` or `x0` separately
at each call site when the source value is held in another register. The generated wrapper
preserves the caller's link register and places a pointer return value in `x1` for the original
call-site code to consume.

```cpp
enum { kCppFunction = 2 };

extern "C" const char *select_text(unsigned value) {
    return value >= 10 ? "enabled" : "disabled";
}

extern "C" void init(void) {
    new_cpp_func_id(kCppFunction, select_text);
    patch_asm(0x100000200, "mov w0, w8");
    patch_asm_func(0x100000204, kCppFunction);
}
```

The numeric ID is local to one compiled unit and connects a `new_asm_func_id` or
`new_cpp_func_id` declaration with one or more `patch_asm_func` declarations. It is metadata
used during layout; it is not a runtime address.

### Assembly Addressing

Assembly bodies are assembled as one unit, so local labels are preferred for internal control
flow. A numeric address in a patch is interpreted as the target virtual address for supported
PC-relative instructions such as `B`, `BL`, `CBZ`, `CBNZ`, and `ADRP`:

```cpp
patch_asm(0x100000300, "b 0x100000380");
patch_asm(0x100000304, "adrp x1, 0x100100000");
```

The framework converts these operands to the required PC-relative encoding and reports an
assembler diagnostic when the instruction or offset is not encodable.

### Expected Bytes

The `expected` form protects a fixed-address patch from being applied to the wrong version:

```cpp
patch_asm(0x100500000, "nop", ".long 0x34000428");
```

If the original instruction is not `0x34000428`, the framework reports a mismatch and stops that
patch. This is a version guard, not an address migration mechanism; use symbols, signatures, or
version rules for migration.

### Migration Flow

For each patch, the framework reads the current input binary, resolves the actual address through
Objective-C metadata, symbols, Swift names, or a unique signature, then plans the hook window,
generates the trampoline, and runs AArch64 relocation at that resolved address. CFG and Function IR
fingerprints can be stored in user version rules to confirm candidates or generate new signatures;
the framework does not currently rewrite fixed addresses from a fingerprint by itself.

The framework also exposes AArch64 CFG analysis:

```cpp
auto graph = armcave::aarch64::analyze_cfg(code, base, entry);
auto fingerprint = armcave::aarch64::cfg_fingerprint(graph);
```

CFG fingerprints can be stored with user rules and call-site anchors to confirm candidates and
build migration rules after a target binary update.

The project can later add an auxiliary locator based on string features and an LLM: search for a
stable string, follow its Xrefs to find the referencing instructions and enclosing function range,
then provide the complete bytes from that candidate range to the LLM. Given the known post-patch
bytes for the target version, the instruction context, and the function range, the LLM can infer the
specific patch address. The existing Patch tool would still perform the actual modification. This
avoids requiring an exact cross-version match for the entire function: finding the exact address is
reduced to finding an approximate region, while the LLM resolves the address within that region.
Repeated, localized, or ambiguous strings should preserve all candidate functions and context; any
LLM-produced address must be checked against the original bytes, instruction boundaries, and
expected bytes. Patching must be skipped when validation fails, with candidates and failure reasons
reported.

Function-level IR is built on top of the CFG and stores the entry, basic blocks, call targets,
constant references, string references, return points, and a fingerprint:

```cpp
#include "aarch64/function_ir.h"

auto function = armcave::aarch64::analyze_function(binary, entry);
for (auto call : function.calls) {
    auto target = call;
}
```

`discover_functions(binary)` starts with the entry point and defined symbols, then builds an IR
for each discovered function. The IR is framework-owned, so version migration rules can store
the function fingerprint, call targets, and reference sets without duplicating analysis in a plugin.

The Apple parser recognizes `LC_DYLD_CHAINED_FIXUPS`, walks common ARM64/ARM64e page chains, and
decodes rebases, binds, import ordinals, symbols, addends, pointer formats, and authenticated
pointers. Adding plugin segments adjusts the chained-fixups file offset inside `__LINKEDIT` and
preserves the original chain data.

Apple metadata APIs:

```cpp
#include "apple_metadata.h"

auto method = armcave::find_objc_method(binary, "PlayerManager", "update:");
auto swift = armcave::find_swift_metadata(binary, "PlayerManager");
```

A plugin can declare an Objective-C method hook directly:

```cpp
extern "C" void init(void) {
    replace_function(match("Player::Damage"), replacement, x0);
    hook_objc_method("PlayerManager", "update:", on_update, x0, x1);
}
```

The framework parses Objective-C method lists, class lists, category lists, and IMP pointers.
The Swift API enumerates reflection strings and metadata references in `__swift5_*`; executable
Swift functions still require a symbol or byte signature locator.

## Plugin SDK

`armcave.h` includes the runtime-free public SDK in `include/armcave_sdk.h` for common plugin operations:

```cpp
armcave_text_length(text);
armcave_text_equals(text, length, "prefix");
armcave_text_starts_with(text, "file:");
armcave_copy_text(out, capacity, text, length);
armcave_append_text(out, capacity, length, text, text_length);
armcave_is_space(c);
armcave_trim_span(begin, end);
armcave_safe_asset_path(path);
armcave_grow_capacity(current, required, initial, result);
```

These functions are `static inline`, so they do not introduce a framework runtime dependency or
libc requirement into a plugin.

Apple and Android share the same hook, locator, JSON, and text APIs. A platform plugin keeps its
own asset loading, object layout, string ABI, and hook address. The JSON helpers provide
platform-neutral lookup and numeric fallback; none of these helpers contains target-binary
fields or addresses.

Asset loading uses `armcave_asset_reader` and `armcave_asset_binary_reader`. Shared code sees only
opaque storage and text pointers; target-function bindings and manager lookup remain in each plugin
adapter, while `armcave_string` selects the target string layout automatically.

## Patch Script

Simple replacements can use `patch.toml` without writing `init` or hook macros:

```toml
[target]
binary = "build/game"
output = "build/game.patched"

[[hook]]
function = "Player::Damage"
replace = "damage.cpp"
handler = "replacement"
registers = ["x0", "w1"]

[[hook]]
signature = "FD 7B BF A9 ?? ?? ?? ?? FD 03 00 91"
detour = "tick.cpp"
handler = "on_tick"
registers = ["x0"]
```

`damage.cpp` only defines the handler. The framework generates a temporary plugin, resolves a
unique symbol, demangled C++ name, or unique signature, generates the wrapper, and reuses the same
segment, trampoline, and relocation pipeline. `signature` locates a hook across code-layout
versions; `class` and `selector` locate Objective-C methods:

```bash
./build/armcave --script patch.toml
```

## Segments and Data

Plugins can request a stable logical segment prefix with `SEGMENT_NAME`:

```cpp
#define SEGMENT_NAME gameplay
```

The framework creates an independent code segment for every plugin. If logical names collide,
it adds a stable short suffix. Static variables, initialized data, and zerofill data are placed
in an independent writable data segment; code and constants remain in an R-X segment. Different
plugins may use the same `replacement`, `init`, or handler names without symbol collisions.

Each plugin is compiled and parsed independently with its own symbol table and relocation map. A
hook handler must resolve inside its own plugin object; duplicate code or data segment names fail
instead of falling back to another plugin's symbols or state. Multiple plugins may target the same
address, but the dispatcher only connects handlers from their respective segments.

## Diagnostics

Failures and expected-value mismatches are emitted as structured JSON, for example:

```json
{"level":"warning","stage":"match","message":"expected instruction mismatch","address":"0x100000498","type":"asm_expected","context":"current=0x... expected=0x..."}
```

Diagnostics include the stage, address, relocation or match type, and context. The command-line
tool still returns a non-zero status when patching fails.

## Build Configuration

`armcave.conf` supports multiple independent profiles:

```text
build_dir = build

[android]
enable = true
input = ArmCaveHook-Arcplugins/binaries/libcocos2dcpp.so-original
output = ArmCaveHook-Arcplugins/binaries/libcocos2dcpp.so
plugins = ArmCaveHook-Arcplugins/plugins/android

[apple]
enable = false
input = ArmCaveHook-Arcplugins/binaries/Arc-mobile.mac-catalyst
output = ArmCaveHook-Arcplugins/binaries/Arc-mobile.patched
plugins = ArmCaveHook-Arcplugins/plugins/apple
```

Each profile has its own input, output, and plugin directory. Optional
`plugin_whitelist` and `plugin_blacklist` values accept comma-separated plugin filenames.

Build requirements:

- CMake 3.24 or newer
- A C++17 compiler
- Clang and Clang++ for AArch64 plugin objects
- CMake, Clang, and MSVC tools on Windows

## Completed

- [x] Apple 64-bit Mach-O plugin injection
- [x] Android 64-bit ELF plugin injection
- [x] AArch64 instruction decoder, relocator, and CFG analyzer
- [x] 4/12/20-byte far-jump sequences
- [x] Conditional branches, ADR/ADRP, and literal-load relocation
- [x] Plugin branch veneers and independent code/data segments
- [x] Duplicate-symbol isolation across plugins
- [x] Independent Apple and Android scene loaders
- [x] Multi-profile build configuration and independent plugin directories
- [x] Dynamic symbols, PLT/GOT, byte signatures, and CFG fingerprints
- [x] Function-level IR, function discovery, call/constant/string/return indexes
- [x] Mach-O chained-fixup analysis, chain walking, and writer offset maintenance
- [x] Objective-C class/method/category metadata and Swift metadata analysis
- [x] `patch.toml` automation layer and minimal handler-source workflow
- [x] Structured diagnostics

## TODO

- [ ] Document Mach-O code-signature invalidation and codesign/ldid/enterprise re-signing workflows
- [ ] Add honest byte-signature stability guidance and a signature survival estimator
- [ ] Extract candidate bytes from string Xrefs, call an LLM to infer cross-version patch addresses, and validate and apply them through the Patch tool
- [ ] Provide an optional lightweight C++ toolkit for plugins
- [ ] Expand Android ELF coverage and documentation for DT_RELR and eh_frame

## License

MIT
