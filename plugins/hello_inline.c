/*
 * Inline Hook 插件 — 含 HOOK_ADDR
 * 在目标地址处打断原指令，跳入 Code Cave 执行此函数，
 * 执行完毕后恢复原指令并跳回原程序继续执行。
 * HOOK_ADDR 请替换为实际要 Hook 的虚拟地址。
 */
#define SEGMENT_NAME hello_inline
#define HOOK_ADDR 0x4E8
#include "stdio.h"
__attribute__((used))
static int hook_entry(int arg0) {
    // arg0 = x0 寄存器在 Hook 点的值
    printf("%d", arg0);
    return -1;
}
