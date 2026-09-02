# Location and Relocation

Prefer a symbol, Objective-C method, or unique byte signature over a fixed address
when a patch must survive binary layout changes.

```cpp
extern "C" void on_tick(void *object) {}

extern "C" void init(void) {
    hook_detour_signature(
        "FD 7B BF A9 ?? ?? ?? ?? FD 03 00 91 ?? ?? ?? ??", on_tick, x0);
}
```

Signatures must match exactly one executable location. Wildcard PC-relative bytes
in `B`, `BL`, `ADR`, `ADRP`, and literal-load instructions when they vary by build.

The patcher expands branch transfers when needed and relocates common AArch64
branches, `ADR`/`ADRP`, and literal loads for detours. Use an expected-byte guard
for fixed-address patches:

```cpp
patch_asm(0x100500000, "nop", ".long 0x34000428");
```
