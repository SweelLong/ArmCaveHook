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

## 分析与重定位

公开分析头文件可用于围绕 patch pipeline 构建工具：

| 头文件 | 主要接口 |
| --- | --- |
| `binary_image.h` | `BinaryImage::parse`、section、segment、symbol、import、chained fixup、地址映射与写入。 |
| `signature.h` | `parse_signature`、`find_signature_matches`、`find_unique_signature`。 |
| `aarch64/decoder.h` | AArch64 指令解码和分支分类。 |
| `aarch64/encoder.h` | 分支范围检查、分支和地址序列构造。 |
| `aarch64/relocator.h` | `relocate_block` 和最大重定位大小估算。 |
| `aarch64/cfg.h` / `function_ir.h` | CFG、函数 IR、函数发现和 fingerprint。 |
| `apple_metadata.h` | Objective-C 与 Swift metadata 枚举和查找。 |
| `symbols.h` | 可用符号列表、目标查找和插件 relocation 解析。 |

当 patch 需要适应二进制布局变化时，优先使用符号、Objective-C 方法或唯一字节签名，而不是固定地址：

```cpp
hook_detour_signature(
    "FD 7B BF A9 ?? ?? ?? ?? FD 03 00 91 ?? ?? ?? ??", on_tick, x0);
```

签名必须只匹配一个可执行位置。不同构建中会变化的 `B`、`BL`、`ADR`、`ADRP` 和 literal-load
中的 PC-relative 字节应使用通配符。patcher 会在需要时扩展远跳，并重定位常见分支、地址指令和
literal load。
