# ArmCaveHook

ARM 跨平台静态 Inline Hook 自动化框架，专为已编译 AArch64 二进制（Android ELF / iOS Mach-O）设计。

**插件化 · 无头文件 · 全自动 · 零汇编**

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
- **自动 Hook**：原指令打掉换 B → Code Cave 执行插件 → 恢复原指令 → B 回下一指令
- **零汇编**：核心逻辑用纯 C 编写，clang 编译为裸机 ARM64 机器码
- **静态无依赖**：直接修改二进制文件，运行时无需任何插件或服务

## 工作流

1. 编写插件：在 `plugins/` 新建 C 文件，顶部写 `#define` 宏，下方写 Hook 逻辑
2. 运行主控：Python 自动遍历、解析宏、编译、注入
3. 输出成品：保存修改后的二进制，iOS 需重签名，Android 直接使用

## 插件格式

```c
#define SEGMENT_NAME nulcorepivot
#define SEGMENT_SIZE 0x1000
#define HOOK_ADDR   0x123456
#define HOOK_SIZE   0x4

__attribute__((used))
static int my_hook(int x) {
    return x + 1;
}
```

宏说明：

| 宏 | 必填 | 说明 |
|---|---|---|
| `SEGMENT_NAME` | 是 | 段核心名，Mach-O 自动加 `__` 前缀，ELF 自动加 `.` 前缀 |
| `SEGMENT_SIZE` | 否 | 预留段大小（默认 0x1000），实际会按编译结果自动计算 |
| `HOOK_ADDR` | 否 | 目标 hook 地址（不填则用 entrypoint） |
| `HOOK_SIZE` | 否 | Hook 窗口大小，必须 ≥ 原指令长度且 4 字节对齐（默认 0x4） |

## 用法

```bash
python3 armcave.py Arc-mobile -o output.bin
python3 armcave.py Arc-mobile --dry-run
```

## 目录

```text
ArmCaveHook/
├── armcave.py          # 主入口
├── tools/
│   ├── pipeline.py     # 流水线调度
│   ├── plugin.py       # 插件解析
│   ├── compiler.py     # clang 编译
│   ├── segment.py      # 段管理（Mach-O / ELF）
│   └── patcher.py      # Hook 跳转编码与回写
├── plugins/
│   ├── sample_hook.c
│   └── second_hook.c
└── Arc-mobile          # 目标二进制
```
