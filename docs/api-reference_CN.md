# API 参考

插件应包含 `armcave.h`。以下是公开 API；实现头文件仍是最终定义来源。

| API | 作用 |
| --- | --- |
| `hook_replace(addr, handler, ...)` | 调用 handler 后直接返回，不执行被覆盖的原逻辑。 |
| `hook_detour(addr, handler, ...)` | 调用 handler，搬迁被覆盖的原指令，然后回到原函数。 |
| `hook_replace_signature(pattern, handler, ...)` | 用唯一 AArch64 字节签名定位目标后替换。 |
| `hook_detour_signature(pattern, handler, ...)` | 用唯一 AArch64 字节签名定位目标后 detour。 |
| `hook_replace_symbol(symbol, handler, ...)` | 按唯一目标函数符号或可反解 C++ 名称替换。 |
| `hook_detour_symbol(symbol, handler, ...)` | 按唯一目标函数符号或可反解 C++ 名称 detour。 |
| `replace_function(match(symbol), handler, ...)` | `hook_replace_symbol` 的函数级 DSL 写法。 |
| `detour_function(match(symbol), handler, ...)` | `hook_detour_symbol` 的函数级 DSL 写法。 |
| `hook_objc_method(class_name, selector, handler, ...)` | 解析 Apple Objective-C method metadata 后替换 IMP。 |
| `hook_detour_objc_method(class_name, selector, handler, ...)` | 解析 Apple Objective-C metadata 后 detour IMP。 |
| `patch_asm(addr, "...")` | 写入编译后的 AArch64 指令。 |
| `patch_asm(addr, "...", "expected")` | 仅在原始指令序列匹配时写入。 |
| `patch_asm_signature(signature, "...")` | 用唯一字节签名定位后写入 AArch64 汇编指令。 |
| `patch_asm_signature(signature, "...", "expected")` | 带 expected 字节校验的签名汇编 patch。 |
| `patch_asm_func(addr, id)` | 将调用点替换为调用已注册汇编或 C++ 函数的 `BL`。 |
| `new_asm_func_id(id, ["...", "..."])` | 注册支持局部 label 的纯汇编函数。 |
| `new_cpp_func_id(id, handler)` | 使用稳定 ID 注册 C++ 函数。 |
| `patch_adrl_data(addr, xN, variable)` | 将两条指令替换为加载插件全局变量地址到 `xN` 的 `ADRP + ADD`。 |
| `patch_asm_data(addr, xN, variable)` | `patch_adrl_data` 的兼容别名。 |
| `match(value)` | 为 `replace_function` 或 `detour_function` 包装符号表达式。 |
| `bind_func_by_sym(ret, name, args, symbol)` | 绑定目标普通函数、C++ 函数或导入函数符号。 |
| `bind_func_by_addr(ret, name, args, addr)` | 绑定固定目标函数地址并处理生成的分支 relocation。 |
| `bind_obj_by_sym(type, name, symbol)` | 绑定目标全局对象或静态对象。 |
| `resolve_addr(va)` | 生成目标数据地址引用，处理 ADRP/PAGEOFF12 relocation。 |
| `read_mem<T>(addr)` / `write_mem<T>(addr, value)` | 访问目标内存。 |
| `armcave_timer_now_ms(...)` | 按目标计时器布局读取毫秒时间并处理缺省值。 |
| `resolve_vfunc(obj, offset)` | 读取对象虚表函数。 |
| `read_typeinfo(obj)` | 读取 Itanium C++ ABI typeinfo。 |
| `armcave_string` / `armcave_string_make` / `armcave_string_data` / `armcave_string_size` / `armcave_string_destroy` | 按目标格式处理 Apple 或 Android 24 字节字符串 ABI。 |
| `armcave_file_manager_get` | 使用 `armcave_string` 路径调用兼容的目标文件管理器 getter。 |
| `logf(format, ...)` | 通过当前平台日志接口写入插件诊断信息。 |
| `armcave_sys_write` / `armcave_utoa` / `armcave_vformat` | 供 `logf` 使用的底层日志和整数格式化 helper。 |
| `armcave_json_value(json, key, out, size)` | 从数字 key 的 JSON 对象中读取字符串。 |
| `armcave_json_or_integer(json, key, label, size, fallback)` | 优先读取 JSON 标签，否则格式化整数 key。 |
| `armcave_json_copy_or_integer(json, key, out, size)` | 将 JSON 标签或整数回退写入公共文本缓冲区。 |
| `armcave_asset_reader` helpers | 跨平台资源生命周期接口，由平台 adapter 实现 open/close。 |
| `armcave_asset_binary_reader` helpers | 提供二进制资源长度检查、分段读取、分配和释放。 |
| `armcave_load_rating_list` | 将有长度限制的 rating-list 资源写入调用方缓冲区。 |

寄存器参数会由框架生成 wrapper，并按声明顺序进入普通 AArch64 参数寄存器：

```cpp
hook_detour(0x10087038c, on_tick, x20, w21);
```

Hook 和汇编详细示例见 [Hook API](hook-api_CN.md)；插件全局数据见
[插件数据地址](plugin-data_CN.md)。
