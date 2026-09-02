# 开始使用

在 `armcave.conf` 中配置一个或多个 profile，然后运行 `./build.sh`。每个已启用的 profile
都有独立的输入二进制、输出二进制和插件目录。

```text
[apple]
enable = true
input = path/to/Arc-mobile
output = path/to/Arc-mobile.patched
plugins = plugins/apple
```

```cpp
#include "armcave.h"

extern "C" void replacement(void *object) {}

extern "C" void init(void) {
    hook_detour(0x100000498, replacement, x0);
}
```

列出的寄存器会按顺序成为普通 AArch64 handler 参数。`hook_replace` 不执行原逻辑；
`hook_detour` 会重定位被覆盖的指令后再回到原函数。
