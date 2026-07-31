# ArmCaveHook

<p align="center">
  <img src="https://img.shields.io/badge/Arch-ARM64%20%7C%20AArch64-blue?logo=arm" alt="ARM64">
  <img src="https://img.shields.io/badge/Apple-Apple%20Mach--O-lightgrey?logo=apple" alt="Apple">
  <img src="https://img.shields.io/badge/Android-Android%20ELF-lightgrey?logo=android" alt="Android">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-blue?logo=cplusplus" alt="C++17">
  <img src="https://img.shields.io/badge/CMake-3.24+-blue?logo=cmake" alt="CMake">
  <img src="https://img.shields.io/badge/License-MIT-yellow?logo=opensourceinitiative" alt="License">
  <img src="https://img.shields.io/github/last-commit/SweelLong/ArmCaveHook?logo=git" alt="Last Commit">
  <img src="https://img.shields.io/github/repo-size/SweelLong/ArmCaveHook?logo=hackthebox" alt="Repo Size">
  <img src="https://img.shields.io/badge/Docs-English%20%7C%20中文-brightgreen?logo=readthedocs" alt="Docs">
</p>

中文 | [English](README_EN.md)

ArmCaveHook 是面向 AArch64 的静态二进制 patch 框架。它把 C++ 插件编译成独立的代码段和数据段，分析原始指令，生成 trampoline，处理 relocation，最后写回 64 位 Mach-O 或 ELF 文件。

目标架构只有 ARM64/AArch64。Apple 使用 64 位 Mach-O，Android 使用 64 位 ELF；不提供 x86、32 位 ARM 或其他目标架构支持。宿主机可以是 macOS、Linux 或 Windows，插件始终按 AArch64 目标编译。

## 快速开始

```bash
git clone --recursive https://github.com/SweelLong/ArmCaveHook.git
cd ArmCaveHook
./build.sh
```

执行前先在 `armcave.conf` 中将目标 profile 设置为 `enable = true`。

`ArmCaveHook-Arcplugins` 是独立的插件子模块。Apple 和 Android 的 `arc_scene_loader.cpp` 是两个完全独立的插件源文件，分别拥有自己的 ABI、地址、数据布局和 hook 声明，不通过源码包含共享实现。

也可以直接构建命令行工具：

```bash
cmake -S . -B build
cmake --build build -j2
```

## 架构

```text
Plugin .cpp
    |
    v
Clang AArch64 object
    |
    +-- metadata parser
    +-- symbol and PLT/GOT resolver
    +-- byte signature resolver
    +-- AArch64 decoder and CFG analyzer
    +-- Function IR and function discovery
    +-- instruction relocator
    +-- Mach-O chained fixups and Apple metadata
    +-- patch.toml script layer
    +-- code/data segment planner
    +-- Mach-O or ELF writer
    v
Patched ARM64 binary
```

框架保留当前内置的 Mach-O/ELF 解析与写回实现，不强制依赖 LIEF 或 LLVM 二进制解析库。外部后端可以作为后续适配参考，但不会改变当前插件格式和 ARM64 patch pipeline。

## 最小插件

插件只需要包含 `armcave.h`，声明 handler，并在 `init(void)` 中声明 patch：

```cpp
#include "armcave.h"

extern "C" int replacement(int value) {
    return value + 1;
}

extern "C" void init(void) {
    hook_replace(0x100000498, replacement, w0);
}
```

框架负责插件独立编译、符号映射、段名冲突处理、handler wrapper、代码段和数据段容量规划。插件不需要手写 cave、保存寄存器、回跳或 relocation。

## Hook API

| API | 作用 |
|---|---|
| `hook_replace(addr, handler, ...)` | 调用 handler 后直接返回，不执行被覆盖的原逻辑。 |
| `hook_detour(addr, handler, ...)` | 调用 handler，搬迁被覆盖的原指令，然后回到原函数。 |
| `hook_replace_signature(pattern, handler, ...)` | 用唯一 AArch64 字节签名定位目标后替换。 |
| `hook_detour_signature(pattern, handler, ...)` | 用唯一 AArch64 字节签名定位目标后 detour。 |
| `hook_replace_symbol(symbol, handler, ...)` | 按唯一目标函数符号或可反解 C++ 名称替换。 |
| `hook_detour_symbol(symbol, handler, ...)` | 按唯一目标函数符号或可反解 C++ 名称 detour。 |
| `replace_function(match(symbol), handler, ...)` | `hook_replace_symbol` 的函数级 DSL 写法。 |
| `detour_function(match(symbol), handler, ...)` | `hook_detour_symbol` 的函数级 DSL 写法。 |
| `hook_objc_method(class_name, selector, handler, ...)` | 解析 Apple Objective-C method metadata 后替换 IMP。 |
| `hook_detour_objc_method(class_name, selector, handler, ...)` | 解析 Apple Objective-C method metadata 后 detour IMP。 |
| `patch_asm(addr, "...")` | 写入编译后的 AArch64 指令。 |
| `patch_asm(addr, "...", "expected")` | 仅在原始指令序列匹配 expected 时写入。 |
| `bind_func_by_sym(ret, name, args, symbol)` | 绑定目标中的普通符号、C++ 符号或导入函数。 |
| `bind_func_by_addr(ret, name, args, addr)` | 绑定固定地址；框架会处理生成的 `B/BL` relocation。 |
| `bind_obj_by_sym(type, name, symbol)` | 绑定目标全局对象或静态对象。 |
| `resolve_addr(va)` | 生成目标数据地址引用，处理 ADRP/PAGEOFF12 relocation。 |
| `read_mem<T>(addr)` / `write_mem<T>(addr, value)` | 访问目标内存。 |
| `armcave_timer_now_ms(...)` | 按目标计时器布局读取毫秒时间并处理缺省值。 |
| `resolve_vfunc(obj, offset)` | 读取对象虚表函数。 |
| `read_typeinfo(obj)` | 读取 Itanium C++ ABI typeinfo。 |
| `armcave_string` / `armcave_string_make` / `armcave_string_data` / `armcave_string_size` / `armcave_string_destroy` | 按目标格式自动处理 Apple 或 Android 的 24 字节字符串 ABI。 |
| `armcave_json_value(json, key, out, size)` | 从数字 key 的 JSON 对象中读取字符串。 |
| `armcave_json_or_integer(json, key, label, size, fallback)` | 优先读取 JSON 标签，否则格式化整数。 |
| `armcave_json_copy_or_integer(json, key, out, size)` | 使用 JSON 标签或整数回退直接写入公共文本缓冲区。 |
| `armcave_asset_reader` / `armcave_asset_load` / `armcave_asset_release` | 跨平台资源读取生命周期接口，平台 adapter 自己实现 open/close。 |
| `armcave_asset_binary_reader` / `armcave_asset_binary_load` | 复用二进制资源的长度检查、分段读取、分配和释放流程。 |
| `armcave_load_rating_list(reader, path, key, out, size)` | 通过平台资源 adapter 读取 JSON 并按整数键复制标签，找不到时回退为数字。 |

寄存器参数会由框架生成 wrapper，按声明顺序移动到 `x0` 至 `x7`：

```cpp
hook_detour(0x10087038c, on_tick, x20, w21);
```

`hook_detour` 会根据 cave 距离自动覆盖 4、12 或 20 字节，并搬迁完整覆盖窗口。插件 handler 不需要知道原始窗口大小。

## AArch64 重定位

重定位器支持：

- `B`、`BL`
- `B.cond`
- `CBZ`、`CBNZ`
- `TBZ`、`TBNZ`
- `ADR`、`ADRP`
- 常见 `LDR` literal 形式
- 基本块内部目标重映射

跳转序列按目标距离选择：

```text
4 bytes:  B/BL target
12 bytes: ADRP x16, target; ADD x16, x16, #pageoff; BR/BLR x16
20 bytes: MOVZ/MOVK x16, absolute; BR/BLR x16
```

条件跳转越界时会反转条件并接绝对跳转。`ADR`、`ADRP` 和 literal load 越界时会改写为绝对地址或寄存器寻址序列。插件自身 `ARM64_RELOC_BRANCH26` 越界时，框架在插件文本中生成 veneer，并按最坏大小预留空间。

## 版本无关定位

框架不会把旧版本的固定地址自动猜成新版本地址。插件必须为每个目标位置声明定位方式：

| 定位方式 | 适用场景 | 版本迁移能力 |
|---|---|---|
| 固定地址 | 已知版本、内部函数没有符号 | 只适用于相同布局版本 |
| 目标符号 | 导出符号、C++ 符号、导入函数 | 通常可跨版本 |
| Objective-C 类名 + selector | Apple Objective-C 方法 | 通常可跨版本 |
| 唯一字节签名 | 没有稳定符号的 AArch64 函数入口 | 可跨代码布局变化的版本 |
| expected 指令 | 保护固定地址 patch | 只做校验，不负责迁移 |

### 字节签名

没有稳定符号时使用 `hook_replace_signature` 或 `hook_detour_signature`：

```cpp
extern "C" void on_tick(void *object) {
    read_mem<void *>(reinterpret_cast<addr_t>(object));
}

extern "C" void init(void) {
    hook_detour_signature(
        "FD 7B BF A9 ?? ?? ?? ?? FD 03 00 91 ?? ?? ?? ??",
        on_tick, x0);
}
```

签名字节之间用空格、逗号或分号分隔；`?`、`??` 和 `*` 表示通配字节。AArch64 的
`BL`、`B`、`ADR`、`ADRP` 和 literal load 中包含版本相关的 PC-relative immediate，
签名通常应该把这些字节写成通配符，同时保留函数序言、寄存器操作和返回路径等稳定字节。

签名只扫描可执行 section，并且必须得到一个结果：零个结果表示签名失效，多个结果表示
签名不够具体，两种情况都会停止 patch，不会随机选择地址。

### 符号定位

目标保留符号时优先使用符号 API：

```cpp
bind_func_by_sym(void, target_update, (void *), "_ZN6Player6updateEv");

extern "C" void replacement(void *player) {
    target_update(player);
}

extern "C" void init(void) {
    hook_replace_symbol("_ZN6Player6updateEv", replacement, x0);
}
```

`bind_func_by_addr`、`hook_replace(addr, ...)` 和 `hook_detour(addr, ...)` 仍然是固定地址
API。它们不会自动寻找新版本位置；插件中的目标函数地址、字段偏移和 ABI 变化也必须单独
处理。

### 指令校验

`expected` 用于防止错误版本被修改：

```cpp
patch_asm(0x100500000, "nop", ".long 0x34000428");
```

如果原始指令不是 `0x34000428`，框架会报告 mismatch 并停止该 patch。它是版本保护机制，
不是地址迁移机制；需要迁移时应改用符号、签名或版本规则。

### 迁移流程

每次 patch 时，框架会读取当前输入二进制，按 Objective-C、符号、Swift 名称或唯一签名
解析实际地址，再在该地址规划 hook window、生成 trampoline 并执行 AArch64 relocation。CFG
和 Function IR fingerprint 可以保存到用户自己的版本规则中，用于确认候选函数或生成新的
签名，但当前不会单独根据 fingerprint 自动改写固定地址。

框架还提供 AArch64 CFG 分析 API：

```cpp
auto graph = armcave::aarch64::analyze_cfg(code, base, entry);
auto fingerprint = armcave::aarch64::cfg_fingerprint(graph);
```

CFG fingerprint 可以和用户自己的版本规则、调用点锚点及签名一起保存，用于升级目标二进制
后的候选确认和迁移规则生成。

函数级 IR 建立在 CFG 之上，统一保存入口、基本块、调用目标、常量引用、字符串引用、返回点和 fingerprint：

```cpp
#include "aarch64/function_ir.h"

auto function = armcave::aarch64::analyze_function(binary, entry);
for (auto call : function.calls) {
    auto target = call;
}
```

`discover_functions(binary)` 会优先使用入口和定义符号收集函数，再为每个函数构建 IR。IR 不依赖插件，版本迁移规则可以直接保存 `function.fingerprint`、调用目标和引用集合。

Apple parser 会识别 `LC_DYLD_CHAINED_FIXUPS`，遍历常见 ARM64/ARM64e page chain，解析 rebase、bind、import ordinal、symbol、addend、pointer format 和 authenticated pointer。添加插件段时会同步调整 chained-fixups 数据在 `__LINKEDIT` 中的文件偏移，并保留原始链数据。

Apple metadata API：

```cpp
#include "apple_metadata.h"

auto method = armcave::find_objc_method(binary, "PlayerManager", "update:");
auto swift = armcave::find_swift_metadata(binary, "PlayerManager");
```

插件只需要声明：

```cpp
extern "C" void init(void) {
    replace_function(match("Player::Damage"), replacement, x0);
    hook_objc_method("PlayerManager", "update:", on_update, x0, x1);
}
```

Objective-C method list、class list、category list 和 IMP 会被框架解析。Swift API 用于枚举 `__swift5_*` 反射字符串和 metadata 引用；Swift 可执行函数仍要求符号或签名定位。

## 插件 SDK

`armcave.h` 自动包含无运行时依赖的公共 SDK `include/armcave_sdk.h`，插件不必重复实现这些函数：

```cpp
armcave_text_length(text);
armcave_text_equals(text, length, "prefix");
armcave_text_starts_with(text, "file:");
armcave_copy_text(out, capacity, text, length);
armcave_append_text(out, capacity, length, text, text_length);
armcave_is_space(c);
armcave_trim_span(begin, end);
armcave_safe_asset_path(path);
armcave_grow_capacity(current, required, initial, result);
```

这些函数是 `static inline`，会直接进入插件代码段，不引入 libc 或框架运行时依赖。

Apple 和 Android 共用同一套 hook、定位、JSON 和文本 API。平台插件只适配各自的资源读取、
对象布局、字符串 ABI 和 hook 地址；`armcave_json_copy_or_integer` 负责跨平台 JSON 标签
查找与整数回退，`armcave_load_rating_list` 负责复用资源读取、解析和释放流程；这些函数
都不包含任何目标二进制字段或地址。

资源读取使用 `armcave_asset_reader` 和 `armcave_asset_binary_reader` 抽象。公共逻辑只处理
opaque storage 和文本指针；目标函数绑定和 manager 获取仍留在插件 adapter 内部，字符串布局
由 `armcave_string` 根据目标格式自动选择。

## Patch Script

简单替换可以不写 `init` 和 hook 宏，使用 `patch.toml`：

```toml
[target]
binary = "build/game"
output = "build/game.patched"

[[hook]]
function = "Player::Damage"
replace = "damage.cpp"
handler = "replacement"
registers = ["x0", "w1"]

[[hook]]
signature = "FD 7B BF A9 ?? ?? ?? ?? FD 03 00 91"
detour = "tick.cpp"
handler = "on_tick"
registers = ["x0"]
```

`damage.cpp` 只定义 handler；框架会生成临时插件、解析唯一符号、C++ demangled name 或
唯一签名、生成 wrapper，并复用同一套 segment、trampoline 和 relocation pipeline。`signature`
用于版本无关的 hook 点定位，`class` 和 `selector` 用于 Objective-C 方法。运行：

```bash
./build/armcave --script patch.toml
```

## 段和数据

可以通过 `SEGMENT_NAME` 请求稳定的逻辑段名前缀：

```cpp
#define SEGMENT_NAME gameplay
```

框架会为每个插件生成独立的代码段。相同逻辑段名前缀发生冲突时自动追加稳定短后缀。插件的静态变量、初始化数据和 zerofill 数据进入独立的 RW 数据段；代码和常量进入 R-X 段。不同插件可以同时使用同名 `replacement`、`init` 或 handler，不会互相覆盖。

每个插件单独编译、单独解析 object、单独维护符号表和 relocation map。Hook handler 必须在
自己的插件 object 中解析成功；代码段或数据段重名会直接失败，不会回退到其他插件的符号
或状态。插件可以挂到同一个目标地址，但 dispatcher 只连接各自段内的 handler。

## 诊断

失败和 expected mismatch 会输出一行结构化 JSON，例如：

```json
{"level":"warning","stage":"match","message":"expected instruction mismatch","address":"0x100000498","type":"asm_expected","context":"current=0x... expected=0x..."}
```

字段包括阶段、地址、重定位或匹配类型和上下文。命令行仍以非零退出码表示 patch 失败。

## 构建配置

`armcave.conf` 支持多个独立 profile：

```text
build_dir = build

[android]
enable = true
input = ArmCaveHook-Arcplugins/binaries/libcocos2dcpp.so-original
output = ArmCaveHook-Arcplugins/binaries/libcocos2dcpp.so
plugins = ArmCaveHook-Arcplugins/plugins/android

[apple]
enable = false
input = ArmCaveHook-Arcplugins/binaries/Arc-mobile.mac-catalyst
output = ArmCaveHook-Arcplugins/binaries/Arc-mobile.patched
plugins = ArmCaveHook-Arcplugins/plugins/apple
```

每个 profile 都有独立的输入、输出和插件目录。可选的 `plugin_whitelist` 和 `plugin_blacklist` 用逗号分隔插件文件名。

构建要求：

- CMake 3.24 或更高版本
- C++17 编译器
- Clang/Clang++，用于生成 AArch64 插件对象
- Windows 使用 CMake、Clang 和 MSVC 工具链

## 完成项

- [x] Apple 64 位 Mach-O 插件注入
- [x] Android 64 位 ELF 插件注入
- [x] AArch64 指令 decoder、relocator 和 CFG analyzer
- [x] 4/12/20 字节远跳 trampoline
- [x] 条件跳转、ADR/ADRP、literal load relocation
- [x] 插件 branch veneer 和独立代码/数据段
- [x] 多插件同名符号隔离
- [x] Apple/Android 独立 scene loader
- [x] 多 profile 构建配置和独立插件目录
- [x] 动态符号、PLT/GOT、字节签名和 CFG fingerprint 定位能力
- [x] 函数级 IR、函数发现、调用/常量/字符串/返回点索引
- [x] Mach-O chained fixups 分析、链遍历和写回偏移维护
- [x] Objective-C class/method/category metadata 和 Swift metadata 分析
- [x] `patch.toml` 自动脚本层和最小化 handler source 工作流
- [x] 结构化诊断日志

## TODO

- [ ] 补充 Mach-O 代码签名失效、codesign/ldid/企业证书重签名流程与提示
- [ ] 增加字节签名稳定性说明和签名存活率估算工具
- [ ] 提供可选的轻量级 C++ 插件工具集
- [ ] 补充 Android ELF 的 DT_RELR、eh_frame 等覆盖度与文档

## License

MIT
