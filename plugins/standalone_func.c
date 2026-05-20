/*
 * 独立函数插件 — 无 HOOK_ADDR
 * 编译后的机器码直接放入新增的 Segment，不修改原程序任何指令。
 * 用于向二进制中注入全新的辅助函数。
 */
#define SEGMENT_NAME standalone_func

__attribute__((used))
static int my_add(int x, int y) {
    return x + y;
}
