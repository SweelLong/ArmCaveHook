# 插件 SDK

`armcave.h` 会包含 `armcave_sdk.h`，其中提供无需框架运行时的插件辅助函数。

```cpp
char out[64] = {};
size_t size = armcave_text_length("prefix");
armcave_copy_text(out, sizeof(out), "prefix", size);
armcave_append_text(out, sizeof(out), size, "-value", 6);
```

## Helper 参考

| API | 作用 |
| --- | --- |
| `armcave_itoa` | 将整数格式化到调用方提供的缓冲区。 |
| `armcave_json_value` / `armcave_json_or_integer` / `armcave_json_copy_or_integer` | 读取整数 key 的 JSON 标签，并支持数字回退。 |
| `armcave_text_length` / `armcave_text_equals` / `armcave_text_starts_with` / `armcave_copy_text` / `armcave_append_text` | 执行有边界的文本操作。 |
| `armcave_is_space` / `armcave_trim_span` | 清理字符范围首尾空白。 |
| `armcave_parse_u32` / `armcave_parse_float` | 解析有边界的数字文本。 |
| `armcave_valid_identifier` / `armcave_safe_asset_path` | 校验标识符和相对资源路径。 |
| `armcave_grow_capacity` | 计算安全的扩容容量。 |
| `armcave_asset_reader` / `armcave_asset_load` / `armcave_asset_release` | 通过平台实现的 open/close 回调读取文本资源。 |
| `armcave_asset_binary_reader` / `armcave_asset_binary_load` / `armcave_asset_binary_size` / `armcave_asset_binary_release` | 通过平台回调读取并管理二进制资源数据。 |
| `armcave_load_rating_list` | 通过两种 asset reader 之一读取有上限的 rating-list 资源。 |

字符串 ABI helper（`armcave_string_make`、`armcave_string_data`、
`armcave_string_size`、`armcave_string_destroy`）以及内存、虚表、计时器与日志 helper
见 [API 参考](api-reference_CN.md)。
