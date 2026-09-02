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

Register assembly and C++ functions by name; branch instructions in `patch_asm`
can reference that name directly:

```cpp
extern "C" int replacement_value(void) { return 1; }

extern "C" void init(void) {
    new_cpp_func(replacement_value);
    patch_asm(0x100000400, "bl replacement_value");
}
```

The recommended way to load the address of a plugin global uses the `adrl`
pseudo-instruction in `patch_asm`:

```cpp
int gV2WarningTag = 0;

extern "C" void init(void) {
    patch_asm(0x100000500, "adrl x0, gV2WarningTag");
}
```

Assembly data directives can patch strings and literal bytes directly. Common
directives include `.ascii`, `.asciz`, `.byte`, `.hword`/`.short`, `.word`/`.long`,
and `.quad`:

```cpp
patch_asm(0x101376059, ".ascii \"Srcaea\"");
patch_asm(0x100000800, ".byte 0x01, 0x02, 0x03, 0x04");
```

The explicit `ADRP + ADD` form is also supported; `:lo12:` denotes the
variable's offset within its 4KB page:

```cpp
int gV2WarningTag = 0;

extern "C" void init(void) {
    patch_asm(0x100000500,
              "adrp x0, gV2WarningTag; add x0, x0, :lo12:gV2WarningTag");
}
```
