#pragma once
/*
 * ArmCaveHook 内置辅助函数 (auto-included)
 *
 * arm_logf(fmt, ...)  — printf 风格格式化输出
 *   支持: %% %d %i %u %x %X %s %c %p  以及 l 前缀 (%ld %lu %lx)
 */

// ── external variadic declarations ──
// Without these, the compiler treats variadic functions as non-variadic,
// so on ARM64 args don't land on the stack where the real implementation reads them.
extern int _printf(const char *fmt, ...);

// ── raw write syscall (macOS / iOS ARM64) ──

__attribute__((always_inline))
static inline void __arm_sys_write(const char *p, int len) {
    asm volatile(
        "mov x16, #4\n"
        "mov x0, #1\n"
        "mov x1, %[buf]\n"
        "mov x2, %[len]\n"
        "svc #0x80"
        :
        : [buf] "r"((long)p), [len] "r"((long)len)
        : "x0", "x1", "x2", "x16", "memory", "cc");
}

// ── formatting helpers (static → local functions, not inlined) ──

static int __arm_itoa(char *buf, int val) {
    int pos = 0;
    if (val < 0) {
        buf[pos++] = '-';
        val = -val;
    }
    char tmp[12];
    int tpos = 0;
    do {
        tmp[tpos++] = '0' + (char)(val % 10);
        val /= 10;
    } while (val > 0);
    while (tpos > 0) buf[pos++] = tmp[--tpos];
    return pos;
}

static int __arm_utoa(char *buf, unsigned long val, int base, int upper) {
    int pos = 0;
    char tmp[32];
    int tpos = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        tmp[tpos++] = digits[val % (unsigned long)base];
        val /= (unsigned long)base;
    } while (val > 0);
    if (base == 16) {
        buf[pos++] = '0';
        buf[pos++] = 'x';
    }
    while (tpos > 0) buf[pos++] = tmp[--tpos];
    return pos;
}

#define __ARM_FMT_BUF_SZ 256

static int __arm_vformat(char *buf, int size, const char *fmt, __builtin_va_list args) {
    int pos = 0;
    char c;
    while ((c = *fmt++) && pos < size - 1) {
        if (c != '%') { buf[pos++] = c; continue; }
        if (!*fmt) break;

        char f = *fmt++;
        int is_long = 0;
        if (f == 'l') { is_long = 1; f = *fmt++; }

        switch (f) {
        case '%': buf[pos++] = '%'; break;
        case 'd': case 'i':
            if (is_long)
                pos += __arm_itoa(buf + pos, (int)__builtin_va_arg(args, long));
            else
                pos += __arm_itoa(buf + pos, __builtin_va_arg(args, int));
            break;
        case 'u':
            if (is_long)
                pos += __arm_utoa(buf + pos, __builtin_va_arg(args, unsigned long), 10, 0);
            else
                pos += __arm_utoa(buf + pos, __builtin_va_arg(args, unsigned int), 10, 0);
            break;
        case 'x':
            if (is_long)
                pos += __arm_utoa(buf + pos, __builtin_va_arg(args, unsigned long), 16, 0);
            else
                pos += __arm_utoa(buf + pos, __builtin_va_arg(args, unsigned int), 16, 0);
            break;
        case 'X':
            if (is_long)
                pos += __arm_utoa(buf + pos, __builtin_va_arg(args, unsigned long), 16, 1);
            else
                pos += __arm_utoa(buf + pos, __builtin_va_arg(args, unsigned int), 16, 1);
            break;
        case 's': {
            const char *s = __builtin_va_arg(args, const char *);
            if (!s) s = "(null)";
            while (*s && pos < size - 1) buf[pos++] = *s++;
            break;
        }
        case 'c':
            buf[pos++] = (char)__builtin_va_arg(args, int);
            break;
        case 'p': {
            void *p = __builtin_va_arg(args, void *);
            pos += __arm_utoa(buf + pos, (unsigned long)p, 16, 0);
            break;
        }
        default:
            buf[pos++] = '%';
            if (is_long) buf[pos++] = 'l';
            buf[pos++] = f;
            break;
        }
    }
    buf[pos] = '\0';
    return pos;
}

// ── public API: format string ──

__attribute__((always_inline))
static inline void arm_logf(const char *fmt, ...) {
    char buf[__ARM_FMT_BUF_SZ];
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    int len = __arm_vformat(buf, sizeof(buf), fmt, args);
    __builtin_va_end(args);
    __arm_sys_write(buf, len);
}
