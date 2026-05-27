/*
 * Detour Hook 插件 — 使用 HOOK_DETOUR
 * 执行 Hook 后不再跳回原函数，完全接管目标函数。
 * 适用于：替换函数逻辑、禁用功能、返回值劫持等场景。
 */
#define SEGMENT_NAME hello_detour
#define HOOK_ADDR 0x460
#define HOOK_DETOUR 1

__attribute__((used))
static int detour_entry(int arg0) {
    _printf("[detour] arg0=%d, original function SKIPPED!\n", arg0);
    return 0;
}
