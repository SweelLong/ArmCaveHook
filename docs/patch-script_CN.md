# Patch Script

简单的 handler patch 可使用 `patch.toml`，无需手写插件元数据。

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

`damage.cpp` 只需要定义 handler。执行方式：

```bash
./build/armcave --script patch.toml
```
