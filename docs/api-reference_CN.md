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
| `patch_asm(addr, "...")` | 写入编译后的 AArch64 指令；`b`/`bl` 等分支可以直接引用注册的函数名，`adrl` 可以引用插件函数或数据符号并展开为 `ADRP + ADD`。助记符不区分大小写，`0x` 绝对目标可大小写混用。 |
| `patch_asm(addr, "...", "expected")` | 仅在原始指令序列匹配时写入。 |
| `patch_hex(addr, chunk, ...)` | 直接覆盖原始字节。多个 chunk 依次拼接后解析；每段支持空格、逗号/分号分隔、`0x` 前缀或 `0000` 连写形式，不区分大小写。chunk 可为字符串字面量或编译期辅助函数产出的 constexpr 字符数组，计算所得 payload 无需手写十六进制。 |
| `new_asm_func(name, ["...", "..."])` | 按名称注册支持局部 label 的纯汇编函数。 |
| `new_cpp_func(handler, ...)` | 按 handler 名称注册 C++ 函数，供 `patch_asm` 的分支指令引用；可选寄存器参数会生成 wrapper，重排调用点寄存器。 |
| `match(value)` | 为 `replace_function` 或 `detour_function` 包装符号表达式。 |
| `bind_func_by_sym(ret, name, args, symbol)` | 绑定目标普通函数、C++ 函数或导入函数符号。 |
| `bind_func_by_addr(ret, name, args, addr)` | 绑定固定目标函数地址并处理生成的分支 relocation。 |
| `bind_obj_by_sym(type, name, symbol)` | 绑定目标全局对象或静态对象。 |
| `resolve_addr(va)` | 生成目标数据地址引用，处理 ADRP/PAGEOFF12 relocation。 |
| `read_mem<T>(addr)` / `write_mem<T>(addr, value)` | 访问目标内存。 |
| `resolve_vfunc(obj, offset)` | 读取对象虚表函数。 |
| `read_typeinfo(obj)` | 读取 Itanium C++ ABI typeinfo。 |
| `logf(format, ...)` | 通过当前平台日志接口写入插件诊断信息。 |
| `armcave_sys_write` / `armcave_utoa` / `armcave_vformat` | 供 `logf` 使用的底层日志和整数格式化 helper。 |
寄存器参数会由框架生成 wrapper，并按声明顺序进入普通 AArch64 参数寄存器：

```cpp
hook_detour(0x10087038c, on_tick, x20, w21);
```

Hook、汇编和插件全局数据详细示例见 [Hook API](hook-api_CN.md)。

### 插件项目 API

状态标记：`✅` 推荐使用，`❌` 不推荐，`⚠️` 可能存在问题。

| API | 作用 | 状态 |
| --- | --- | --- |
| `armcave_json_value` / `armcave_json_or_integer` / `armcave_json_copy_or_integer` | 读取整数 key 的 JSON 标签，并支持数字回退。 | ✅ |
| `armcave_asset_reader` helpers | 提供文本资源的跨平台打开、关闭和生命周期管理。 | ✅ |
| `armcave_asset_binary_reader` helpers | 提供二进制资源长度检查、分段读取、分配和释放。 | ✅ |
| `armcave_load_rating_list` | 将有长度限制的 rating-list 资源写入调用方缓冲区。 | ✅ |
| `armcave_timer_now_ms` | 按目标计时器布局读取毫秒时间并处理缺省值。 | ⚠️ |
| `armcave_string` ABI helpers | 处理目标游戏的 Apple/Android 24 字节字符串 ABI。 | ⚠️ |
