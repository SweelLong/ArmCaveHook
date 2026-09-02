# Getting Started

Configure one or more profiles in `armcave.conf`, then run `./build.sh`. Each
enabled profile has its own input binary, output binary, and plugin directory.

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

Listed registers become normal AArch64 handler arguments. `hook_replace` skips
the original logic; `hook_detour` relocates overwritten instructions and resumes it.
