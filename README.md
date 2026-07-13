<p align="center">
  <img src="https://img.shields.io/badge/Arch-ARM64%20%7C%20AArch64-blue?logo=arm" alt="ARM64">
  <img src="https://img.shields.io/badge/Apple-iOS%20Mach--O-lightgrey?logo=apple" alt="Apple">
    <img src="https://img.shields.io/badge/Android-Android%20ELF-lightgrey?logo=android" alt="Android">
  <img src="https://img.shields.io/badge/Language-Python%203-yellow?logo=python" alt="Python">
  <img src="https://img.shields.io/badge/Binary-LIEF-orange?logo=bookstack" alt="LIEF">
</p>

# ArmCaveHook

ArmCaveHook 是一个 AArch64 静态 hook 框架。插件使用 `.cpp` 编写，框架把插件编译成 cave 代码段，再按插件入口声明修改目标二进制。

## 插件规范

插件开头必须包含标准库头文件：

```cpp
#include "armcave.h"
```

顶层只放 include、`#define SEGMENT_NAME`、类型声明、函数声明、全局变量、函数定义和 `init(void)`。除变量和声明外，不要把执行逻辑写在函数外面。对二进制的任何修改都写进 `init(void)`。

```cpp
#include "armcave.h"

#define SEGMENT_NAME testhook

extern "C" int replacement(int a, int b) {
    logf("a=%d b=%d\n", a, b);
    return a + b;
}

void init(void) {
    hook(0x100000498, replacement, w0, w1);
}
```

`SEGMENT_NAME` 是新增 cave 名称。Mach-O 使用 `__testhook`，ELF 使用 `.testhook`。段大小由框架根据编译后的代码、常量数据、重定位和 wrapper 自动计算。

## 标准 API

| API | 作用 |
|---|---|
| `hook(addr, handler, ...)` | 函数替代 hook，从目标地址跳到 handler。 |
| `cave(handler, ...)` | 只把 handler 写入 cave，不修改目标控制流。 |
| `inject_asm(addr, "instruction")` | 汇编 AArch64 指令并写入虚拟地址。 |
| `inject_hex(addr, "hex")` | 写入十六进制机器码。 |
| `target_fn(ret, name, args, symbol)` | 按符号名声明目标二进制里的函数。 |
| `target_obj(name, symbol)` | 按符号名声明目标二进制里的通用对象。 |
| `target_obj(type, name, symbol)` | 按符号名声明目标二进制里的强类型对象。 |
| `target_obj_fn(name, symbol, ...)` | 按符号名声明目标对象函数。 |
| `target_obj_call(fn, obj, ...)` | 调用目标对象函数。 |
| `target_call(ret, addr, args, ...)` | 按地址快速调用目标内部函数。 |
| `logf(fmt, ...)` | 简单日志输出。 |
| `vector<T>` | 固定容量 C++ 容器，默认容量 32。 |
| `string` | 固定容量字符串，容量 127 字节。 |
| `u8/u16/u32/u64/i8/i16/i32/i64/addr_t` | 基础类型别名。 |

`hook` 和 `cave` 后面的寄存器参数可选。传入寄存器后，框架会生成 wrapper，把这些寄存器移动到标准 AArch64 调用参数寄存器。

## 指令注入

```cpp
#define SEGMENT_NAME patchseg

void init(void) {
    inject_asm(0x100500000, "NOP");
    inject_asm(0x100500004, "MOV W0, #1; RET");
    inject_hex(0x100500010, "1f2003d5");
}
```

`inject_asm` 用来写 AArch64 汇编文本，`inject_hex` 用来写已经确认好的机器码。

默认跳转使用 AArch64 `B` 指令。`B` 是 26-bit 相对跳转，按 4 字节指令对齐计算，范围是当前位置前后 128 MiB。

## 目标函数调用

`asm("symbol")` 是 Clang/C++ 的符号别名语法，用来把插件声明绑定到目标二进制里的真实符号。它不是指令注入 API；指令注入使用 `inject_asm`。

```cpp
target_obj(cout_obj, "__ZNSt3__14coutE");
target_obj_fn(cout_put, "__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE3putEc", char);
target_obj_fn(cout_flush, "__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE5flushEv");

extern "C" int hook_func(int a, int b) {
    string out = "hook ！！\n";
    for (u64 i = 0; i < out.size(); ++i) {
        target_obj_call(cout_put, cout_obj, out[i]);
    }
    target_obj_call(cout_flush, cout_obj);
    return a + b;
}
```

目标内部函数只有地址时可以用 `target_call`：

```cpp
static int call_internal(int a, int b) {
    return target_call(int, 0x100012340, (int, int), a, b);
}
```

使用这类接口时要保证地址、符号名、ASLR 状态、调用约定和参数类型正确。

## 组合方式

只新增函数时使用 `cave`：

```cpp
#define SEGMENT_NAME toolseg

static void inspect(int value) {
    logf("value=%d\n", value);
}

void init(void) {
    cave(inspect, w0);
}
```

需要 inline 行为时，先用 `cave` 写入函数，再用 `inject_asm` 写跳转：

```cpp
#define SEGMENT_NAME inlinehook

static void before_call(int value) {
    logf("value=%d\n", value);
}

void init(void) {
    cave(before_call, w0);
    inject_asm(0x100123450, "B 0x100800000");
}
```

## C++ 使用范围

插件按 C++17 编译，并关闭异常、RTTI 和线程安全静态初始化。推荐使用简单类型、普通函数、固定容量 `vector<T>` 和 `string`。避免依赖堆分配、异常、完整 libc++ 容器和复杂全局构造。

## CLI

```bash
python3 armcave.py binaries/AppBinary --dry-run
python3 armcave.py binaries/AppBinary -o binaries/AppBinary.patched
python3 armcave.py binaries/AppBinary --plugins plugins
```

## Web IDE

```bash
python3 webui.py
```

打开：

```text
http://127.0.0.1:5000
```

## TODO

- [ ] 支持长范围跳转。长跳转需要多条指令，例如加载绝对地址后 `br`，会占用更大的覆盖窗口，也会带来原指令保存、回跳和对齐限制。
- [ ] 扩展其他架构后端。
