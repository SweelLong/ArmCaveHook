# API Reference

Include `armcave.h` in a plugin. The macro and helper declarations below are
the public API; the implementation headers remain the source of truth.

| API | Description |
| --- | --- |
| `hook_replace(addr, handler, ...)` | Calls the handler and returns without executing overwritten original logic. |
| `hook_detour(addr, handler, ...)` | Calls the handler, relocates overwritten instructions, then resumes the original function. |
| `hook_replace_signature(pattern, handler, ...)` | Finds one unique AArch64 byte signature and replaces the matched location. |
| `hook_detour_signature(pattern, handler, ...)` | Finds one unique AArch64 byte signature and detours the matched location. |
| `hook_replace_symbol(symbol, handler, ...)` | Replaces a unique target symbol or demangled C++ function. |
| `hook_detour_symbol(symbol, handler, ...)` | Detours a unique target symbol or demangled C++ function. |
| `replace_function(match(symbol), handler, ...)` | Function-level DSL spelling of `hook_replace_symbol`. |
| `detour_function(match(symbol), handler, ...)` | Function-level DSL spelling of `hook_detour_symbol`. |
| `hook_objc_method(class_name, selector, handler, ...)` | Resolves Apple Objective-C metadata and replaces the method IMP. |
| `hook_detour_objc_method(class_name, selector, handler, ...)` | Resolves Apple Objective-C metadata and detours the method IMP. |
| `patch_asm(addr, "...")` | Writes assembled AArch64 instructions; `b`/`bl` branches may directly reference registered functions, and `adrl` may reference plugin functions or data symbols and expands to `ADRP + ADD`. Mnemonics are case-insensitive; absolute `0x` targets may be upper or lower case. |
| `patch_asm(addr, "...", "expected")` | Writes only after matching the original instruction sequence. |
| `patch_hex(addr, chunk, ...)` | Overwrites raw bytes directly. Chunks are concatenated before parsing; each accepts spaced, comma/semicolon-separated, `0x`-prefixed, or contiguous nibble pairs such as `0000`, case-insensitive. Chunks may be string literals or constexpr char arrays from a compile-time helper, so computed payloads work without hand-writing hex. |
| `new_asm_func(name, ["...", "..."])` | Registers a pure-assembly function by name, with local labels. |
| `new_cpp_func(handler, ...)` | Registers a C++ function by handler name for branch references from `patch_asm`; optional register arguments generate a wrapper that remaps call-site registers. |
| `match(value)` | Wraps a symbol expression for `replace_function` or `detour_function`. |
| `bind_func_by_sym(ret, name, args, symbol)` | Binds a normal, C++, or imported target function symbol. |
| `bind_func_by_addr(ret, name, args, addr)` | Binds a fixed target function address and resolves generated branch relocations. |
| `bind_obj_by_sym(type, name, symbol)` | Binds a global or static target object. |
| `resolve_addr(va)` | Generates a target data-address reference with ADRP/PAGEOFF12 relocation. |
| `read_mem<T>(addr)` / `write_mem<T>(addr, value)` | Reads or writes target memory. |
| `resolve_vfunc(obj, offset)` | Reads a function pointer from an object's vtable. |
| `read_typeinfo(obj)` | Reads Itanium C++ ABI typeinfo. |
| `logf(format, ...)` | Writes a plugin diagnostic message through the selected platform logger. |
| `armcave_sys_write` / `armcave_utoa` / `armcave_vformat` | Low-level logging and integer-format helpers used by `logf`. |
Register arguments are copied by a generated wrapper into normal AArch64 argument
registers in declaration order:

```cpp
hook_detour(0x10087038c, on_tick, x20, w21);
```

For detailed hook, assembly, and plugin-data examples, see [Hook API](hook-api.md).
