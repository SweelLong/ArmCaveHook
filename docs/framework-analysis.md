# Framework Analysis Interfaces

The public analysis headers support tooling built around the patch pipeline.

| Header | Main interfaces |
| --- | --- |
| `binary_image.h` | `BinaryImage::parse`, sections, segments, symbols, imports, chained fixups, virtual/file address mapping, and writing. |
| `signature.h` | `parse_signature`, `find_signature_matches`, and `find_unique_signature`. |
| `aarch64/decoder.h` | AArch64 instruction decoding and branch classification. |
| `aarch64/encoder.h` | Branch-range checks and branch/address sequence construction. |
| `aarch64/relocator.h` | `relocate_block` and maximum relocated-size estimation. |
| `aarch64/cfg.h` | CFG construction, function IR, and fingerprints from raw code. |
| `aarch64/function_ir.h` | `analyze_function` and `discover_functions` from a parsed binary. |
| `apple_metadata.h` | Objective-C method enumeration/lookup and Swift metadata enumeration/lookup. |
| `symbols.h` | Available-symbol listing, target lookup, and plugin relocation resolution. |

Example: discover functions and inspect a fingerprint.

```cpp
#include "binary_image.h"
#include "aarch64/function_ir.h"

auto binary = BinaryImage::parse("game");
auto functions = armcave::aarch64::discover_functions(*binary);
if (!functions.empty()) {
    auto fingerprint = functions.front().fingerprint;
}
```

For Apple metadata, use `find_objc_method(binary, "Class", "method:")` to
resolve an Objective-C IMP. Swift executable functions still require a symbol or
a unique byte signature locator.
