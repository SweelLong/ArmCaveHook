<p align="center">
  <img src="https://img.shields.io/badge/Arch-ARM64%20%7C%20AArch64-blue?logo=arm" alt="ARM64">
  <img src="https://img.shields.io/badge/Target-iOS%20Mach--O%20%7C%20Android%20ELF-lightgrey?logo=apple" alt="Target">
  <img src="https://img.shields.io/badge/Engine-Python%203%20%2B%20Clang%2FLLVM-yellow?logo=python" alt="Python+Clang">
  <img src="https://img.shields.io/badge/Binary-LIEF-orange?logo=bookstack" alt="LIEF">
  <img src="https://img.shields.io/badge/Hook-Static%20Inline-red" alt="Static Inline Hook">
</p>

# ArmCaveHook

ARM 跨平台静态 Inline Hook 自动化框架，专为已编译 AArch64 二进制（Android ELF / iOS Mach-O）设计。

**插件化 · 无头文件 · 全自动 · 零汇编**

## 项目介绍

ArmCaveHook 是一个面向 ARM64 平台的静态二进制插桩框架。与传统动态 Hook 工具（如 Frida、Substrate）不同，它直接修改磁盘上的二进制文件，无需运行时依赖。

**工作方式：** 框架读取已编译的 Mach-O / ELF 二进制，在目标地址处将原始指令替换为无条件跳转（B 指令），跳入框架自动新增的 Code Cave 代码段。Cave 内依次调用用户编写的 C 插件函数，执行完毕后恢复被覆盖的原始指令，再跳回原程序继续运行。

**两种注入模式：**

| 模式 | HOOK_ADDR | 行为 |
|---|---|---|
| 独立函数注入 | 不定义 | 只将 C 代码编译为机器码，放入新增段，不修改原程序 |
| Inline Hook | 定义目标地址 | 打断原指令 → 跳入 Cave → 执行插件 → 恢复 → 跳回 |

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
- **两种模式**：
  - **独立函数**（无 `HOOK_ADDR`）：纯粹向二进制注入代码段，不修改原程序
  - **Inline Hook**（含 `HOOK_ADDR`）：原指令打掉换 B → Code Cave 执行插件 → 恢复原指令 → B 回下一指令
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
│   └── hello_inline.c      # Inline Hook 示例
├── binaries/               # 目标二进制文件存放目录
├── static/                 # Web UI 静态资源 (CSS/JS)
└── templates/              # Web UI Jinja2 模板
```

## 插件格式

### 独立函数插件（无 HOOK_ADDR）

仅向二进制注入代码段，不 Hook 任何地址：

```c
#define SEGMENT_NAME myfunc

__attribute__((used))
static int my_add(int x, int y) {
    return x + y;
}
```

### Inline Hook 插件（含 HOOK_ADDR）

在目标地址打断原指令，跳入 Code Cave 执行 Hook 函数：

```c
#define SEGMENT_NAME myhook
#define HOOK_ADDR   0x123456

__attribute__((used))
static int hook_entry(int arg0) {
    // arg0 = x0 寄存器在 Hook 点的值
    if (arg0 > 100) return 0;
    return arg0 * 2;
}
```

宏说明：

| 宏 | 必填 | 说明 |
|---|---|---|
| `SEGMENT_NAME` | **是** | 段核心名，Mach-O 自动加 `__` 前缀，ELF 自动加 `.` 前缀 |
| `HOOK_ADDR` | 否 | 目标 hook 地址（不填则仅注入代码，不修改原程序） |
| `HOOK_SIZE` | 否 | Hook 跳转窗口大小（不填默认 `0x4`，4 字节对齐） |
| `SEGMENT_SIZE` | 否 | 手动指定段大小（不填则自动根据编译后机器码体积计算） |

> 不填 `SEGMENT_SIZE` 时，pipeline 根据 clang 编译后的机器码体积 + 跳转控制开销自动计算，全程零手工。

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
