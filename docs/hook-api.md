# Hook API

The complete public macro surface is in [`include/armcave.h`](../include/armcave.h).

```cpp
extern "C" void on_update(void *object) {}

extern "C" void init(void) {
    hook_replace(0x100000498, on_update, x0);
    hook_detour(0x100000600, on_update, x0);
    patch_asm(0x100000700, "nop");
    patch_asm(0x100000704, "mov w0, #1", ".long 0x34000428");
}
```

`hook_replace` replaces original logic. `hook_detour` resumes through relocated
original instructions. The optional third `patch_asm` argument validates existing
bytes before writing.

Generated assembly and C++ functions use stable IDs:

```cpp
enum { kEnabled = 1 };

extern "C" void init(void) {
    new_asm_func_id(kEnabled, ["mov w0, #1", "ret"]);
    patch_asm_func(0x100000400, kEnabled);
}
```
