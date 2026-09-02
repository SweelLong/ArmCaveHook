# Build Configuration and Diagnostics

`armcave.conf` supports independent profiles:

```text
build_dir = build

[android]
enable = true
input = path/to/libcocos2dcpp.so
output = path/to/libcocos2dcpp.patched.so
plugins = plugins/android
plugin_whitelist = arc_rating.cpp

[apple]
enable = false
input = path/to/Arc-mobile
output = path/to/Arc-mobile.patched
plugins = plugins/apple
```

Every enabled profile needs `input`, `output`, and `plugins`. `plugin_whitelist`
and `plugin_blacklist` accept comma-separated plugin filenames.

Failures and expected-byte mismatches are emitted as structured JSON and cause a
non-zero exit status:

```json
{"level":"warning","stage":"match","message":"expected instruction mismatch","address":"0x100000498","type":"asm_expected","context":"current=0x... expected=0x..."}
```

Mach-O output is ad-hoc signed on macOS when `codesign` is available. Distribution
or installation may still require the target-specific signing workflow.

## Patch Script

Use `patch.toml` for simple handler patches without writing plugin metadata by hand:

```toml
[target]
binary = "build/game"
output = "build/game.patched"

[[hook]]
function = "Player::Damage"
replace = "damage.cpp"
handler = "replacement"
registers = ["x0", "w1"]
```

Run it with `./build/armcave --script patch.toml`. The referenced C++ file only
needs to define the selected handler.
