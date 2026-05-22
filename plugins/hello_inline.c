/*
 * Inline Hook 插件 — 含 HOOK_ADDR
 * 在目标地址处打断原指令，跳入 Code Cave 执行此函数，
 * 执行完毕后恢复原指令并跳回原程序继续执行。
 * HOOK_ADDR 请替换为实际要 Hook 的虚拟地址。
 */
#define SEGMENT_NAME hello_inline
#define HOOK_ADDR 0x460

__attribute__((used))
static int hook_entry(int arg0) {
    // arg0 = x0 寄存器在 Hook 点的值
    _printf("[hook] arg0=%d\n", arg0);
    //arm_logf("[log] arg0=%d\n", arg0);
    return -1;
}
