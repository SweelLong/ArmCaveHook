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
| `patch_asm(addr, "...")` | Writes assembled AArch64 instructions. |
| `patch_asm(addr, "...", "expected")` | Writes only after matching the original instruction sequence. |
| `patch_asm_signature(signature, "...")` | Locates one unique byte signature, then writes assembled AArch64 instructions. |
| `patch_asm_signature(signature, "...", "expected")` | Signature-based assembly patch with expected-byte validation. |
| `patch_asm_func(addr, id)` | Replaces a call site with `BL` to a registered assembly or C++ function. |
| `new_asm_func_id(id, ["...", "..."])` | Registers a pure-assembly function with local labels. |
| `new_cpp_func_id(id, handler)` | Registers a C++ function under a stable ID. |
| `patch_adrl_data(addr, xN, variable)` | Replaces two instructions with `ADRP + ADD` that load a plugin global address into `xN`. |
| `patch_asm_data(addr, xN, variable)` | Compatibility alias for `patch_adrl_data`. |
| `match(value)` | Wraps a symbol expression for `replace_function` or `detour_function`. |
| `bind_func_by_sym(ret, name, args, symbol)` | Binds a normal, C++, or imported target function symbol. |
| `bind_func_by_addr(ret, name, args, addr)` | Binds a fixed target function address and resolves generated branch relocations. |
| `bind_obj_by_sym(type, name, symbol)` | Binds a global or static target object. |
| `resolve_addr(va)` | Generates a target data-address reference with ADRP/PAGEOFF12 relocation. |
| `read_mem<T>(addr)` / `write_mem<T>(addr, value)` | Reads or writes target memory. |
| `armcave_timer_now_ms(...)` | Reads milliseconds using a target timer layout and fallback policy. |
| `resolve_vfunc(obj, offset)` | Reads a function pointer from an object's vtable. |
| `read_typeinfo(obj)` | Reads Itanium C++ ABI typeinfo. |
| `armcave_string` / `armcave_string_make` / `armcave_string_data` / `armcave_string_size` / `armcave_string_destroy` | Handles the Apple or Android 24-byte string ABI selected for the target. |
| `armcave_file_manager_get` | Calls a compatible target file-manager getter with an `armcave_string` path. |
| `logf(format, ...)` | Writes a plugin diagnostic message through the selected platform logger. |
| `armcave_sys_write` / `armcave_utoa` / `armcave_vformat` | Low-level logging and integer-format helpers used by `logf`. |
| `armcave_json_value(json, key, out, size)` | Reads a string from a JSON object using an integer key. |
| `armcave_json_or_integer(json, key, label, size, fallback)` | Uses a JSON label when available, otherwise formats the integer key. |
| `armcave_json_copy_or_integer(json, key, out, size)` | Copies a JSON label or numeric fallback to a platform-neutral buffer. |
| `armcave_asset_reader` helpers | Cross-platform asset lifetime API with platform-owned open/close adapters. |
| `armcave_asset_binary_reader` helpers | Provides binary-asset length checks, chunked reads, allocation, and release. |
| `armcave_load_rating_list` | Loads a bounded rating-list asset into a caller-provided buffer. |

Register arguments are copied by a generated wrapper into normal AArch64 argument
registers in declaration order:

```cpp
hook_detour(0x10087038c, on_tick, x20, w21);
```

For detailed hook and assembly examples, see [Hook API](hook-api.md). For plugin
data globals, see [Plugin data addresses](plugin-data.md).
