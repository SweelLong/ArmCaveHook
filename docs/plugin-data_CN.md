# 插件数据地址

`patch_adrl_data(addr, xN, variable)` 可以让 patch 点直接访问插件自己的全局变量，
无需借用目标程序中的地址。它会将 `addr` 开始的两条指令替换成 `ADRP + ADD`，并将变量地址
写入指定的 64 位寄存器。

```cpp
int gTag = 0;

extern "C" void init(void) {
    patch_adrl_data(0x100000300, x0, gTag);
    // 后续原始指令保持不变，例如 ldr w1, [x0]。
}
```

`xN` 必须是 AArch64 64 位寄存器名，可使用 `x0` 至 `x31`。该 API
固定覆盖 8 字节，因此 patch 点必须至少允许覆盖两条指令。

变量在插件编译时生成，早于 `init()` 执行。所有具有静态存储期的插件可写对象共用插件数据段：
命名空间作用域全局变量和函数内 `static` 变量会写入；`init()` 中的普通局部变量位于运行时栈，
不会写入。patch pipeline 写入插件时会复制初始字节。请使用具有外部可见名称的命名空间作用域
全局变量。`patch_asm_data` 保留为兼容别名。
