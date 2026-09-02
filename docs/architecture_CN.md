# 架构

```text
Plugin .cpp
    |
    v
Clang AArch64 object
    |
    +-- metadata parser
    +-- symbol 和 PLT/GOT resolver
    +-- 字节签名 resolver
    +-- AArch64 decoder、CFG 和 function IR
    +-- 指令 relocator 和 branch veneer
    +-- Mach-O chained fixup 和 Apple metadata
    +-- patch.toml 脚本层
    +-- 代码段/数据段规划
    +-- Mach-O 或 ELF writer
    v
Patched ARM64 binary
```

框架拥有自己的 Mach-O/ELF parser 和 writer，不依赖 LIEF 或 LLVM 作为二进制解析后端。
插件会独立编译和解析，因此符号、代码、可写数据、wrapper、trampoline 和 relocation 都相互隔离。

目标架构仅支持 ARM64/AArch64。Apple 目标使用 64 位 Mach-O，Android 目标使用 64 位 ELF；
不支持 x86 或 32 位 ARM。
