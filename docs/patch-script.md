# Patch Script

Use `patch.toml` for simple handler patches without writing plugin metadata by hand.

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

`damage.cpp` only defines the handler. Run the script with:

```bash
./build/armcave --script patch.toml
```
