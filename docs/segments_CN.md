# 段命名规则

`SEGMENT_NAME` 用于为插件自动生成的代码段和可写数据段指定稳定的逻辑前缀：

```cpp
#define SEGMENT_NAME gameplay
```

## 长度限制

框架内部保存的是不带平台前缀的逻辑段名。

| 目标格式 | 代码段 | 可写数据段 |
| --- | --- | --- |
| Mach-O | `__` + 最多 14 个逻辑字符 | `__` + 逻辑名 + `_data`，逻辑名与后缀合计最多 14 个字符 |
| ELF | `.` + 逻辑名 | `.` + 逻辑名 + `_data` |

Mach-O 的段名字段固定为 16 字节。ArmCaveHook 会占用前两个字节写入 `__` 前缀，因此代码段
的逻辑名最多只能有 14 个 ASCII 字符。可写数据段会追加 `_data`，所以要完整保留其基础名时，
基础名最多只能有 9 个字符。

例如：

```cpp
#define SEGMENT_NAME arcratingpatch
```

会生成 Mach-O 代码段 `__arcratingpatch`。数据段不能命名为
`__arcratingpatch_data`：它连同 `__` 前缀共 21 个字符，超过 Mach-O 的 16 字节限制。
实际生成的数据段为 `__arcrating_data`。

若希望数据段在 `_data` 前完整保留 `SEGMENT_NAME`，请将 `SEGMENT_NAME` 控制在 9 个字符以内。

## 重名处理

段名会规范化为小写 ASCII 字母、数字和下划线。当多个插件请求相同的代码段或数据段名时，
ArmCaveHook 会追加稳定的短哈希后缀；若仍无法保证唯一性，构建会报错。

## 共享洞穴名

ArmCaveHook 不会为每个插件单独开一个段，而是把本次参与 patch 的插件代码 Section 全部打包进
一个 R+X 洞穴、可写数据 Section 全部打包进一个 R+W 洞穴（详见 README 的「段布局」章节）。
洞穴名由插件段名的**最长公共前缀**推导：

1. 取所有插件段名的最长公共前缀，去掉平台前缀（`__` / `.`）得到逻辑前缀；
2. 逻辑前缀不以 `_` 结尾就补一个 `_`；
3. 拼上 `code` / `data`。

| 插件段名 | 洞穴名 |
| --- | --- |
| `__ncp_ap`、`__ncp_cp`、`__ncp_p64`、`__ncp_rs` | `__ncp_code` / `__ncp_data` |
| 只有一个插件 `__ncp_rs` | `__ncp_rs_code` / `__ncp_rs_data` |
| 无公共前缀（如 `__ncp_rs` 与 `__zzzplay`） | 回退 `__armcave_code` / `__armcave_data` |

Mach-O 段名字段固定 16 字节，因此逻辑前缀最多保留 9 个字符，超出会被截断。
Android（ELF）的 `PT_LOAD` 没有名字字段，洞穴按同一规则划分但没有名字可显示。

