# 构建配置与诊断

`armcave.conf` 支持相互独立的 profile：

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

每个启用的 profile 都需要 `input`、`output` 和 `plugins`。`plugin_whitelist` 和
`plugin_blacklist` 接受逗号分隔的插件文件名。

失败和 expected 字节不匹配会输出结构化 JSON，并以非零状态码退出：

```json
{"level":"warning","stage":"match","message":"expected instruction mismatch","address":"0x100000498","type":"asm_expected","context":"current=0x... expected=0x..."}
```

在 macOS 上，如果 `codesign` 可用，Mach-O 输出会进行 ad-hoc 签名。发布或安装仍可能需要
目标平台自己的签名流程。
