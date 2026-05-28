#pragma once

extern int _printf(const char *fmt, ...);

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

static inline int __arm_itoa(char *buf, int val) {
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

static inline int __arm_utoa(char *buf, unsigned long val, int base, int upper) {
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

static inline int __arm_vformat(char *buf, int size, const char *fmt, __builtin_va_list args) {
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
            pos += __arm_itoa(buf + pos, is_long ? (int)__builtin_va_arg(args, long) : __builtin_va_arg(args, int));
            break;
        case 'u':
            pos += __arm_utoa(buf + pos, is_long ? __builtin_va_arg(args, unsigned long) : __builtin_va_arg(args, unsigned int), 10, 0);
            break;
        case 'x':
            pos += __arm_utoa(buf + pos, is_long ? __builtin_va_arg(args, unsigned long) : __builtin_va_arg(args, unsigned int), 16, 0);
            break;
        case 'X':
            pos += __arm_utoa(buf + pos, is_long ? __builtin_va_arg(args, unsigned long) : __builtin_va_arg(args, unsigned int), 16, 1);
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
        case 'p':
            pos += __arm_utoa(buf + pos, (unsigned long)__builtin_va_arg(args, void *), 16, 0);
            break;
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

__attribute__((always_inline))
static inline void arm_logf(const char *fmt, ...) {
    char buf[256];
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    int len = __arm_vformat(buf, sizeof(buf), fmt, args);
    __builtin_va_end(args);
    __arm_sys_write(buf, len);
}

__attribute__((used, section("__TEXT,__caveasm")))
static const unsigned char _cave_asm_data[] = {
    0xfd, 0x7b, 0xbf, 0xa9,
    0xfd, 0x7b, 0xc1, 0xa8,
    0xc0, 0x03, 0x5f, 0xd6,
};

#define BRANCH_GOTO_DST() __asm__ volatile( \
    "mov sp, x29\n" \
    "ldp x29, x30, [sp], #0x10\n" \
    "add sp, sp, #0x10\n" \
    ".word 0xCAFEBABE\n" \
    ::: "memory")

#define BRANCH_GOTO_NEXT() __asm__ volatile( \
    "mov sp, x29\n" \
    "ldp x29, x30, [sp], #0x10\n" \
    "add sp, sp, #0x10\n" \
    ".word 0xDEADCAFE\n" \
    ::: "memory")

#define BRANCH_GOTO_CONV() __asm__ volatile( \
    "mov sp, x29\n" \
    "ldp x29, x30, [sp], #0x10\n" \
    "add sp, sp, #0x10\n" \
    ".word 0xBEEFCAFE\n" \
    ::: "memory")


