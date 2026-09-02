# Hook API

完整的公开宏定义见 [`include/armcave.h`](../include/armcave.h)。

```cpp
extern "C" void on_update(void *object) {}

extern "C" void init(void) {
    hook_replace(0x100000498, on_update, x0);
    hook_detour(0x100000600, on_update, x0);
    patch_asm(0x100000700, "nop");
    patch_asm(0x100000704, "mov w0, #1", ".long 0x34000428");
}
```

`hook_replace` 替换原始逻辑；`hook_detour` 重定位被覆盖的原始指令后继续执行。
`patch_asm` 的可选第三个参数会在写入前校验原始字节。

注册汇编或 C++ 函数时直接使用函数名；`patch_asm` 中的分支指令可以引用该名称：

```cpp
extern "C" int replacement_value(void) { return 1; }

extern "C" void init(void) {
    new_cpp_func(replacement_value);
    patch_asm(0x100000400, "bl replacement_value");
}
```

加载插件全局变量地址推荐使用 `patch_asm` 的 `adrl` 伪指令：

```cpp
int gV2WarningTag = 0;

extern "C" void init(void) {
    patch_asm(0x100000500, "adrl x0, gV2WarningTag");
}
```

汇编数据伪指令也可以直接 patch 字符串和字面量字节。常用类型包括 `.ascii`、`.asciz`、
`.byte`、`.hword`/`.short`、`.word`/`.long` 和 `.quad`：

```cpp
patch_asm(0x101376059, ".ascii \"Srcaea\"");
patch_asm(0x100000800, ".byte 0x01, 0x02, 0x03, 0x04");
```

也可以显式写出 `ADRP + ADD`，其中 `:lo12:` 表示变量在 4KB 页内的偏移：

```cpp
int gV2WarningTag = 0;

extern "C" void init(void) {
    patch_asm(0x100000500,
              "adrp x0, gV2WarningTag; add x0, x0, :lo12:gV2WarningTag");
}
```
