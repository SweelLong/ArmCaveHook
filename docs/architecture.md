# Architecture

```text
Plugin .cpp
    |
    v
Clang AArch64 object
    |
    +-- metadata parser
    +-- symbol and PLT/GOT resolver
    +-- byte signature resolver
    +-- AArch64 decoder, CFG, and function IR
    +-- instruction relocator and branch veneers
    +-- Mach-O chained fixups and Apple metadata
    +-- patch.toml script layer
    +-- code/data segment planner
    +-- Mach-O or ELF writer
    v
Patched ARM64 binary
```

The framework owns its Mach-O/ELF parser and writer; it does not require LIEF or
LLVM as a binary parsing backend. Plugins are compiled and parsed independently,
so their symbols, code, writable data, wrappers, trampolines, and relocations are
isolated from one another.

The target architecture is ARM64/AArch64 only. Apple targets use 64-bit Mach-O;
Android targets use 64-bit ELF. x86 and 32-bit ARM are not supported.

## Analysis And Relocation

The public analysis headers support tooling around the patch pipeline:

| Header | Main interfaces |
| --- | --- |
| `binary_image.h` | `BinaryImage::parse`, sections, segments, symbols, imports, chained fixups, address mapping, and writing. |
| `signature.h` | `parse_signature`, `find_signature_matches`, and `find_unique_signature`. |
| `aarch64/decoder.h` | AArch64 instruction decoding and branch classification. |
| `aarch64/encoder.h` | Branch-range checks and branch/address sequence construction. |
| `aarch64/relocator.h` | `relocate_block` and maximum relocated-size estimation. |
| `aarch64/cfg.h` / `function_ir.h` | CFG construction, function IR, discovery, and fingerprints. |
| `apple_metadata.h` | Objective-C and Swift metadata enumeration and lookup. |
| `symbols.h` | Available-symbol listing, target lookup, and plugin relocation resolution. |

Prefer a symbol, Objective-C method, or unique byte signature over a fixed
address when a patch must survive binary layout changes:

```cpp
hook_detour_signature(
    "FD 7B BF A9 ?? ?? ?? ?? FD 03 00 91 ?? ?? ?? ??", on_tick, x0);
```

Signatures must match exactly one executable location. Use wildcards for
PC-relative bytes in `B`, `BL`, `ADR`, `ADRP`, and literal-load instructions
that vary between builds. The patcher expands far branches when needed and
relocates common AArch64 branches, address instructions, and literal loads.
