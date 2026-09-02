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

生成汇编或 C++ 函数时使用稳定 ID：

```cpp
enum { kEnabled = 1 };

extern "C" void init(void) {
    new_asm_func_id(kEnabled, ["mov w0, #1", "ret"]);
    patch_asm_func(0x100000400, kEnabled);
}
```
