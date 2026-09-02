# Plugin SDK

`armcave.h` includes `armcave_sdk.h`, a runtime-free helper set for plugin code.

```cpp
char out[64] = {};
size_t size = armcave_text_length("prefix");
armcave_copy_text(out, sizeof(out), "prefix", size);
armcave_append_text(out, sizeof(out), size, "-value", 6);
```

## Helper Reference

| API | Purpose |
| --- | --- |
| `armcave_itoa` | Formats an integer into a caller-provided buffer. |
| `armcave_json_value` / `armcave_json_or_integer` / `armcave_json_copy_or_integer` | Reads an integer-keyed JSON label with a numeric fallback. |
| `armcave_text_length` / `armcave_text_equals` / `armcave_text_starts_with` / `armcave_copy_text` / `armcave_append_text` | Performs bounded text operations. |
| `armcave_is_space` / `armcave_trim_span` | Trims a character span. |
| `armcave_parse_u32` / `armcave_parse_float` | Parses bounded numeric text. |
| `armcave_valid_identifier` / `armcave_safe_asset_path` | Validates identifiers and relative asset paths. |
| `armcave_grow_capacity` | Calculates a safe growing allocation capacity. |
| `armcave_asset_reader` / `armcave_asset_load` / `armcave_asset_release` | Reads text assets through platform-defined open/close callbacks. |
| `armcave_asset_binary_reader` / `armcave_asset_binary_load` / `armcave_asset_binary_size` / `armcave_asset_binary_release` | Reads and owns binary asset data through platform callbacks. |
| `armcave_load_rating_list` | Loads a bounded rating-list asset through either asset-reader form. |

String ABI helpers (`armcave_string_make`, `armcave_string_data`,
`armcave_string_size`, and `armcave_string_destroy`) plus memory, vtable, timer,
and logging helpers are listed in the [API reference](api-reference.md).
