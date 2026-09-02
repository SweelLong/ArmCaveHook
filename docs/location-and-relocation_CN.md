# 定位与重定位

当 patch 需要适应二进制布局变化时，优先使用符号、Objective-C 方法或唯一字节签名，而不是固定地址。

```cpp
extern "C" void on_tick(void *object) {}

extern "C" void init(void) {
    hook_detour_signature(
        "FD 7B BF A9 ?? ?? ?? ?? FD 03 00 91 ?? ?? ?? ??", on_tick, x0);
}
```

签名必须只匹配一个可执行位置。`B`、`BL`、`ADR`、`ADRP` 和 literal load 中随构建变化的
PC-relative 字节应使用通配符。

patcher 会在需要时扩展分支跳转，并为 detour 重定位常见的 AArch64 分支、`ADR`/`ADRP` 与
literal load。固定地址 patch 应使用 expected 字节保护：

```cpp
patch_asm(0x100500000, "nop", ".long 0x34000428");
```
