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
