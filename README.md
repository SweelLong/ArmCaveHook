<p align="center">
  <img src="https://img.shields.io/badge/Arch-ARM64%20%7C%20AArch64-blue?logo=arm" alt="ARM64">
  <img src="https://img.shields.io/badge/Apple-iOS%20Mach--O-lightgrey?logo=apple" alt="Apple">
    <img src="https://img.shields.io/badge/Android-Android%20ELF-lightgrey?logo=android" alt="Android">
  <img src="https://img.shields.io/badge/Language-Python%203-yellow?logo=python" alt="Python">
  <img src="https://img.shields.io/badge/Binary-LIEF-orange?logo=bookstack" alt="LIEF">
  <img src="https://img.shields.io/badge/Hook-Static%20Inline-red" alt="Static Inline Hook">
</p>

# ArmCaveHook

ARM 跨平台静态 Inline Hook 自动化框架，专为已编译 AArch64 二进制（Android ELF / iOS Mach-O）设计。

**插件化 · 无头文件 · 全自动 · 零汇编**

## 项目介绍

ArmCaveHook 是一个面向 ARM64 平台的静态二进制插桩框架。与传统动态 Hook 工具（如 Frida、Substrate）不同，它直接修改磁盘上的二进制文件，无需运行时依赖。

**工作方式：** 框架读取已编译的 Mach-O / ELF 二进制，在目标地址处将原始指令替换为无条件跳转（B 指令），跳入框架自动新增的 Code Cave 代码段。Cave 内依次调用用户编写的 C 插件函数，执行完毕后可选择恢复原始指令跳回（Inline Hook）或直接返回调用者（Detour Hook）。

**注入模式与修饰器：**

| 模式 | 所需宏 | 行为 |
|---|---|---|
| Code Injection | 仅 `SEGMENT_NAME` | 只将 C 代码编译为机器码，注入到新增段，不修改原程序 |
| Inline Hook | `HOOK_ADDR` | 打断原指令 → 跳入 Cave → 执行插件 → 恢复原指令并跳回 |
| Detour Hook | `HOOK_ADDR` + `HOOK_DETOUR 1` | 打断原指令 → 跳入 Cave → 执行插件 → 不恢复，不回跳 |
| Branch Host | 加 `HOOK_BRANCH_HOST 1` | **修饰器**，Hook 点首条指令是分支时 NOP 分支而非普通指令，插件通过 `BRANCH_GOTO()` 控制是否跳转 |

> Branch Host 不是独立模式，是叠加在 Inline/Detour 上的修饰器。`BRANCH_GOTO()` 后的行为取决于 `HOOK_DETOUR`：未开启则指令恢复并跳回；开启则不恢复、不回跳。

**适用场景：** iOS/Android 逆向分析、游戏修改、安全研究、二进制插桩教学。由于修改是静态的，修改后的二进制可直接分发，目标设备无需越狱/Root 或安装任何框架。

## 核心架构

三层架构，无单独头文件，所有配置与代码高度聚合：

| 层 | 职责 |
|---|---|
| Python 主控层 (`armcave.py` + `tools/`) | 遍历插件、解析 C 宏定义、调用 clang 编译、通过 LIEF 操作二进制 |
| C 插件层 (`plugins/`) | 单个 C 文件即一个独立功能，宏定义写在顶部，配置即代码 |
| LIEF 底层 | 新增 Code Cave 段、注入机器码、修改跳转指令 |

## 核心特性

- **跨平台**：ARM64 架构，Android ELF + iOS Mach-O 通用
- **专用 Code Cave**：主动新增独立 RX 段，不依赖原程序空闲空间
- **插件化**：`plugins/` 目录放纯 C 文件，新增功能无需修改主控代码
- **全自动解析**：Python 通过正则读取 C 文件顶部 `#define`，自动获取目标地址、偏移等
- **自动扩容**：根据所有插件编译后的机器码大小计算 segment 大小
- **三种模式**：
  - **独立函数**（无 `HOOK_ADDR`）：纯粹向二进制注入代码段，不修改原程序
  - **Inline Hook**（含 `HOOK_ADDR`）：原指令打掉换 B → Code Cave 执行插件 → 恢复原指令 → B 回下一指令
  - **Detour Hook**（含 `HOOK_DETOUR`）：原指令打掉换 B → Cave 执行插件 → 不回放原指令 → 直接 `RET` 给调用者
- **零汇编**：核心逻辑用纯 C 编写，clang 编译为裸机 ARM64 机器码
- **静态无依赖**：直接修改二进制文件，运行时无需任何插件或服务
- **Web 管理界面**：可视化插件编辑、流水线控制、二进制分析

## 目录

```text
ArmCaveHook/
├── armcave.py              # CLI 主入口
├── webui.py                # Web 管理界面 (Flask)
├── run.sh                  # Web UI 一键启动脚本
├── tools/
│   ├── pipeline.py         # 流水线调度
│   ├── plugin.py           # 插件解析
│   ├── compiler.py         # clang 编译
│   ├── segment.py          # 段管理（Mach-O / ELF）
│   ├── patcher.py          # Hook 跳转编码与回写
│   └── symbols.py          # 符号表解析与重定位
├── plugins/
│   ├── armcave.h           # 内置头文件（自动包含）
│   ├── rating_str.c        # Hook 示例：替换难度评星字符串
│   └── rating_unicode.c    # Hook 示例：Unicode codepoint → UTF-8 编码
├── binaries/               # 目标二进制文件存放目录
├── static/                 # Web UI 静态资源 (CSS/JS)
└── templates/              # Web UI Jinja2 模板
```

## 插件格式

### Code Injection 插件

```c
#define SEGMENT_NAME my_add

__attribute__((used))
static int my_add(int x, int y) {
    return x + y;
}
```

### Inline Hook 插件

```c
#define SEGMENT_NAME hook_entry
#define HOOK_ADDR 0x123456

__attribute__((used))
static int hook_entry(int arg0) {
    // arg0 = x0 寄存器在 Hook 点的值
    if (arg0 > 100) return 0;
    return arg0 * 2;
}
```

### Detour Hook 插件

```c
#define SEGMENT_NAME detour_entry
#define HOOK_ADDR 0x123456
#define HOOK_DETOUR 1

__attribute__((used))
static int detour_entry(int arg0) {
    _printf("[detour] arg0=%d\n", arg0);
    return 0;
}
```

### Branch Host 插件

```c
#define SEGMENT_NAME branch_test
#define HOOK_ADDR 0x123456       // 此处是一条 CBZ/B.cond/B.NE 等分支指令
#define HOOK_BRANCH_HOST 1       // 启用分支托管
#define REGISTER_ARGS w0, x19

__attribute__((used))
static void handler(int w0, void* x19) {
    if (w0 >= 5) {
        // 改写字符串后续逻辑...
        BRANCH_GOTO();  // 跳回原始分支目标地址
    }
    // 不调 BRANCH_GOTO() → 分支不成立，继续执行 Cave → 恢复原指令并跳回
    // 若同时开启了 HOOK_DETOUR，BRANCH_GOTO() 跳转后 Cave 结束，不恢复不回跳
}
```

宏说明：

| 宏 | 必填 | 说明 |
|---|---|---|
| `SEGMENT_NAME` | **是** | 段核心名，Mach-O 自动加 `__` 前缀，ELF 自动加 `.` 前缀 |
| `HOOK_ADDR` | 否 | 目标 hook 地址（不填则仅注入代码，不修改原程序） |
| `HOOK_SIZE` | 否 | Hook 跳转窗口大小（不填默认 `0x4`，4 字节对齐） |
| `SEGMENT_SIZE` | 否 | 手动指定段大小（不填则自动根据编译后机器码体积计算） |
| `HOOK_DETOUR` | 否 | 设为 `1` 启用 Detour 模式，执行后不再回放原指令或跳回（默认 Inline） |
| `REGISTER_ARGS` | 否 | 指定参数绑定的寄存器列表（如 `x20, x19`），插件函数参数 `arg0` 自动绑定 `x20`、`arg1` 绑定 `x19`，框架生成 MOV 包装器 |
| `HOOK_BRANCH_HOST` | 否 | 设为 `1` 启用分支托管模式，Hook 点首条分支指令被 NOP，插件通过 `BRANCH_GOTO()` 宏手动控制跳转 |
| `HOOK_NOP` | 否 | 直接 NOP 掉指定地址的指令，值为文件偏移（如 `0xA9B358, 0xA9B35C`），配合 `HOOK_SIZE` 指定 NOP 长度 |

> 不填 `SEGMENT_SIZE` 时，pipeline 根据 clang 编译后的机器码体积 + 跳转控制开销自动计算，全程零手工。

### 重要：`static` 关键字

**所有插件函数必须加 `static`**。不加 `static` 时编译器将函数名导出为全局符号，导致 `__TEXT` 段出现指向插件函数的外部引用，重定位时框架会误解析为跨模块调用而报错。

### BRANCH_GOTO 栈约束

`BRANCH_GOTO()` 通过暴力修改 SP 跳回原始分支目标，要求插件函数满足：

- **不能有额外的栈上局部变量**（除了函数参数本身）
- 编译器(clang)生成的帧必须是标准 `stp x29, x30, [sp, #-0x10]!`
- 如果函数有额外局部变量（超过参数个数），编译器会分配更大栈帧，BRANCH_GOTO 的 SP 修正会算错导致 crash

需要额外局部变量时：改用 `static` 局部变量存储在数据段，不占用栈空间。

### IDA Pro 伪代码到 C 类型对照

逆向中最常见的任务是将 IDA 反编译的伪代码转为 ArmCaveHook 插件代码。核心原则：**IDA 伪代码中 `ptr + N` 永远是字节偏移**，要用 `(char*)` 做指针运算，再 cast 到目标类型。

| IDA 伪代码类型 | 位宽 | C 类型 | 解引用示例 |
|---|---|---|---|
| `_BYTE` | 8 bit | `char` / `uint8_t` | `*(char*)((char*)ptr + 0xN)` |
| `_WORD` | 16 bit | `short` / `uint16_t` | `*(short*)((char*)ptr + 0xN)` |
| `_DWORD` | 32 bit | `int` / `uint32_t` | `*(int*)((char*)ptr + 0xN)` |
| `_QWORD` | 64 bit | `long long` / `uint64_t` / `void*` | `*(long long*)((char*)ptr + 0xN)` |

**错误示例：**
```c
// IDA: v1 = *(_DWORD *)(x0 + 0x110)
// ❌ (int*)x0 + 0x110 是 int 指针运算 = x0 + 0x110 * 4 = x0 + 0x440
int v9 = *(int*)((int*)x0 + 0x110);
```

**正确写法：**
```c
// ✅ char* 指针运算，字节偏移 0x110
int v9 = *(int*)((char*)x0 + 0x110);

// 等于 IDA 的: v1 = *(_DWORD *)(x0 + 0x110)
// 等于原始指令: LDR W0, [X0, #0x110]
```

**REGISTER_ARGS 寄存器类型速查：**

| 寄存器 | 位宽 | C 类型 | 说明 |
|---|---|---|---|
| `w0`-`w30` | 32 bit | `int` / `uint32_t` | 32 位寄存器，不涉及指针 |
| `x0`-`x30` | 64 bit | `void*` / `uint64_t` | 64 位寄存器，可能是指针或数值 |

## 用法

### CLI

```bash
# 独立函数注入（无 HOOK_ADDR 的插件）
python3 armcave.py binaries/Arc-mobile -o binaries/output.bin

# 模拟运行，仅扫描不修改
python3 armcave.py binaries/Arc-mobile --dry-run
```

### Web 管理界面

```bash
./run.sh
# 浏览器访问 http://127.0.0.1:5000
```

Web UI 功能：
- **仪表盘**：项目概览、项目文档（README 渲染）
- **插件管理**：在线创建/编辑/重命名/删除插件，语法高亮编辑器，拖拽排序优先级，编译检查，段名冲突检测
- **可用符号面板**：选择参考二进制后列出所有导入符号 + 内置符号，支持模糊搜索与双击复制
- **文件管理**：拖拽上传/删除二进制文件
- **注入控制**：选择二进制 + 插件组合，模拟运行或执行注入，实时 SSE 日志流，段信息与十六进制预览，一键下载修补后文件

## TODO

- [ ] 兼容更多架构
