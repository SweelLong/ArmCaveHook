# Plugin Data Addresses

`patch_adrl_data(addr, xN, variable)` makes a plugin global variable addressable
from a patch site without using an address from the target binary. It replaces the
two instructions beginning at `addr` with an `ADRP + ADD` pair that writes the
variable address to the specified 64-bit register.

```cpp
int gTag = 0;

extern "C" void init(void) {
    patch_adrl_data(0x100000300, x0, gTag);
    // Existing instructions after the first two remain intact, e.g. ldr w1, [x0].
}
```

`xN` must name an AArch64 64-bit register, from `x0` to `x31`. The API reserves
exactly eight bytes, so the target location must have
two overwriteable instructions.

The variable is emitted when the plugin is compiled, before `init()` runs. All
plugin writable objects with static storage duration share the plugin data
segment: namespace-scope globals and function-local `static` variables are
included; ordinary `init()` locals use stack storage and are not included.
Initial bytes are copied when the patch pipeline writes the plugin. Use a
namespace-scope global with an externally visible name. `patch_asm_data` remains
available as a compatibility alias.
