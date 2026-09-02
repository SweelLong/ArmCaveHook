# 框架分析接口

公开的分析头文件可用于围绕 patch pipeline 构建工具。

| 头文件 | 主要接口 |
| --- | --- |
| `binary_image.h` | `BinaryImage::parse`、section、segment、symbol、import、chained fixup、虚拟/文件地址映射与写入。 |
| `signature.h` | `parse_signature`、`find_signature_matches`、`find_unique_signature`。 |
| `aarch64/decoder.h` | AArch64 指令解码和分支分类。 |
| `aarch64/encoder.h` | 分支范围检查、分支和地址序列构造。 |
| `aarch64/relocator.h` | `relocate_block` 和最大重定位大小估算。 |
| `aarch64/cfg.h` | 从原始代码构建 CFG、函数 IR 和 fingerprint。 |
| `aarch64/function_ir.h` | 从已解析二进制执行 `analyze_function` 和 `discover_functions`。 |
| `apple_metadata.h` | Objective-C 方法枚举/查找和 Swift metadata 枚举/查找。 |
| `symbols.h` | 可用符号列表、目标查找和插件 relocation 解析。 |

示例：发现函数并读取 fingerprint。

```cpp
#include "binary_image.h"
#include "aarch64/function_ir.h"

auto binary = BinaryImage::parse("game");
auto functions = armcave::aarch64::discover_functions(*binary);
if (!functions.empty()) {
    auto fingerprint = functions.front().fingerprint;
}
```

Apple metadata 可使用 `find_objc_method(binary, "Class", "method:")` 解析
Objective-C IMP。Swift 可执行函数仍需要符号或唯一字节签名定位。
