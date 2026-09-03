#pragma once

#include <cstdint>

#ifdef ARMCAVE_ELF
extern "C" int armcave_android_log_print(int priority, const char *tag,
                                           const char *format)
    asm("__android_log_print");
#endif

__attribute__((always_inline))
static inline void armcave_sys_write(const char *p, int len) {
#ifdef ARMCAVE_ELF
    char escaped[512];
    int out = 0;
    for (int i = 0; i < len && out < (int)sizeof(escaped) - 1; ++i) {
        if (p[i] == '%' && out < (int)sizeof(escaped) - 2)
            escaped[out++] = '%';
        escaped[out++] = p[i];
    }
    escaped[out] = '\0';
    armcave_android_log_print(4, "ArmCave", escaped);
#else
    asm volatile(
        "mov x16, #4\n"
        "mov x0, #1\n"
        "mov x1, %[buf]\n"
        "mov x2, %[len]\n"
        "svc #0x80"
        :
        : [buf] "r"((long)p), [len] "r"((long)len)
        : "x0", "x1", "x2", "x16", "memory", "cc");
#endif
}

static inline int armcave_itoa(char *buf, int val) {
    int pos = 0;
    unsigned int value;
    if (val < 0) {
        buf[pos++] = '-';
        value = 0U - (unsigned int)val;
    } else {
        value = (unsigned int)val;
    }
    char tmp[12];
    int tpos = 0;
    do {
        tmp[tpos++] = '0' + (char)(value % 10);
        value /= 10;
    } while (value > 0);
    while (tpos > 0)
        buf[pos++] = tmp[--tpos];
    buf[pos] = '\0';
    return pos;
}

static inline int armcave_utoa(char *buf, unsigned long val, int base, int upper) {
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

static inline int armcave_vformat(char *buf, int size, const char *fmt, __builtin_va_list args) {
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
            pos += armcave_itoa(buf + pos, is_long ? (int)__builtin_va_arg(args, long) : __builtin_va_arg(args, int));
            break;
        case 'u':
            pos += armcave_utoa(buf + pos, is_long ? __builtin_va_arg(args, unsigned long) : __builtin_va_arg(args, unsigned int), 10, 0);
            break;
        case 'x':
            pos += armcave_utoa(buf + pos, is_long ? __builtin_va_arg(args, unsigned long) : __builtin_va_arg(args, unsigned int), 16, 0);
            break;
        case 'X':
            pos += armcave_utoa(buf + pos, is_long ? __builtin_va_arg(args, unsigned long) : __builtin_va_arg(args, unsigned int), 16, 1);
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
            pos += armcave_utoa(buf + pos, (unsigned long)__builtin_va_arg(args, void *), 16, 0);
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

static inline void logf(const char *fmt, ...) {
    char buf[256];
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    int len = armcave_vformat(buf, sizeof(buf), fmt, args);
    __builtin_va_end(args);
    armcave_sys_write(buf, len);
}

__attribute__((used, section("__TEXT,__caveasm")))
static const unsigned char armcave_asm_data[] = {
    0xff, 0x20, 0x03, 0xd5,
    0xfd, 0x7b, 0xbf, 0xa9,
    0xfd, 0x7b, 0xc1, 0xa8,
    0xc0, 0x03, 0x5f, 0xd6,
};

#define armcave_str2(x) #x
#define armcave_str(x) armcave_str2(x)
#define armcave_str_args2(...) #__VA_ARGS__
#define armcave_str_args(...) armcave_str_args2(__VA_ARGS__)
#define armcave_cat2(a, b) a##b
#define armcave_cat(a, b) armcave_cat2(a, b)
#define armcave_unique(prefix) armcave_cat(prefix, __COUNTER__)

#define armcave_meta(kind, addr, handler, segment, ...) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_meta_)[] = \
        kind "|addr=" armcave_str(addr) "|handler=" #handler \
        "|regs=" #__VA_ARGS__ "|segment=" #segment; \
    __attribute__((used, section("__DATA,__armkeep"))) \
    static void *armcave_unique(armcave_keep_) = (void *)&handler

#define armcave_meta_signature(kind, signature, handler, segment, ...) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_meta_signature_)[] = \
        kind "|signature=" signature "|handler=" #handler \
        "|regs=" #__VA_ARGS__ "|segment=" #segment; \
    __attribute__((used, section("__DATA,__armkeep"))) \
    static void *armcave_unique(armcave_signature_keep_) = (void *)&handler

#define armcave_meta_symbol(kind, symbol, handler, segment, ...) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_meta_symbol_)[] = \
        kind "|symbol=" symbol "|handler=" #handler \
        "|regs=" #__VA_ARGS__ "|segment=" #segment; \
    __attribute__((used, section("__DATA,__armkeep"))) \
    static void *armcave_unique(armcave_symbol_keep_) = (void *)&handler

#define armcave_meta_objc(kind, class_name, selector, handler, segment, ...) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_meta_objc_)[] = \
        kind "|objc_class=" class_name "|selector=" selector \
        "|handler=" #handler "|regs=" #__VA_ARGS__ "|segment=" #segment; \
    __attribute__((used, section("__DATA,__armkeep"))) \
    static void *armcave_unique(armcave_objc_keep_) = (void *)&handler


#define armcave_patch_meta(kind, addr, size, payload, segment) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_patch_meta_)[] = \
        kind "|addr=" armcave_str(addr) "|size=" armcave_str(size) \
        "|data=" payload "|segment=" #segment

#define hook_replace(addr, handler, ...) \
    armcave_meta("hook_replace", addr, handler, auto, __VA_ARGS__)

#define hook_detour(addr, handler, ...) \
    armcave_meta("hook_detour", addr, handler, auto, __VA_ARGS__)

#define hook_replace_signature(signature, handler, ...) \
    armcave_meta_signature("hook_replace", signature, handler, auto, __VA_ARGS__)

#define hook_detour_signature(signature, handler, ...) \
    armcave_meta_signature("hook_detour", signature, handler, auto, __VA_ARGS__)

#define hook_replace_symbol(symbol, handler, ...) \
    armcave_meta_symbol("hook_replace", symbol, handler, auto, __VA_ARGS__)

#define hook_detour_symbol(symbol, handler, ...) \
    armcave_meta_symbol("hook_detour", symbol, handler, auto, __VA_ARGS__)

#define armcave_match(value) value
#define match(value) armcave_match(value)

#define replace_function(matcher, handler, ...) \
    hook_replace_symbol(matcher, handler, __VA_ARGS__)

#define detour_function(matcher, handler, ...) \
    hook_detour_symbol(matcher, handler, __VA_ARGS__)

#define hook_objc_method(class_name, selector, handler, ...) \
    armcave_meta_objc("hook_replace", class_name, selector, handler, auto, __VA_ARGS__)

#define hook_detour_objc_method(class_name, selector, handler, ...) \
    armcave_meta_objc("hook_detour", class_name, selector, handler, auto, __VA_ARGS__)


#define armcave_patch_asm(addr, asm_text) \
    armcave_patch_meta("patch_asm", addr, 0, asm_text, auto)

#define armcave_patch_meta_expected(kind, addr, expected, payload, segment) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_patch_meta_expected_)[] = \
        kind "|addr=" armcave_str(addr) "|expected=" expected \
        "|size=0|data=" payload "|segment=" #segment

#define armcave_patch_asm_expected(addr, asm_text, expected) \
    armcave_patch_meta_expected("patch_asm", addr, expected, asm_text, auto)

#define armcave_pick_patch_asm(_1, _2, _3, name, ...) name
#define patch_asm(...) \
    armcave_pick_patch_asm(__VA_ARGS__, armcave_patch_asm_expected, armcave_patch_asm)(__VA_ARGS__)

struct armcave_patch_hex_meta {
    char bytes[1024];
};

constexpr armcave_patch_hex_meta armcave_make_patch_hex_meta(
    const char *addr_text, const char *const *chunks, unsigned count)
{
    armcave_patch_hex_meta meta = {};
    unsigned pos = 0;
    const char *fixed[] = {"patch_hex|addr=", nullptr, "|size=0|data="};
    fixed[1] = addr_text;
    for (unsigned f = 0; f < 3; ++f)
        for (const char *p = fixed[f]; *p && pos + 1 < sizeof(meta.bytes); ++p)
            meta.bytes[pos++] = *p;
    for (unsigned c = 0; c < count; ++c)
        for (const char *p = chunks[c]; *p && pos + 1 < sizeof(meta.bytes); ++p)
            meta.bytes[pos++] = *p;
    for (const char *p = "|segment=auto"; *p && pos + 1 < sizeof(meta.bytes); ++p)
        meta.bytes[pos++] = *p;
    return meta;
}

template <typename... Chunks>
constexpr armcave_patch_hex_meta armcave_make_patch_hex_meta_va(
    const char *addr_text, const char *first, Chunks... rest)
{
    const char *chunks[] = {first, rest...};
    return armcave_make_patch_hex_meta(addr_text, chunks,
                                       (unsigned)(sizeof(chunks) / sizeof(chunks[0])));
}

#define armcave_patch_hex(addr, ...) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static constexpr auto armcave_unique(armcave_patch_hex_meta_) = \
        armcave_make_patch_hex_meta_va(armcave_str(addr), __VA_ARGS__)

#define patch_hex(addr, ...) armcave_patch_hex(addr, __VA_ARGS__)

#define armcave_new_asm_func(name, ...) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_new_asm_meta_)[] = \
        "new_asm_func|name=" #name "|args=" \
        armcave_str_args(__VA_ARGS__)

#define new_asm_func(name, ...) \
    armcave_new_asm_func(name, __VA_ARGS__)

#define armcave_new_cpp_func(handler, ...) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_new_cpp_meta_)[] = \
        "new_cpp_func|name=" #handler "|handler=" #handler \
        "|regs=" #__VA_ARGS__; \
    __attribute__((used, section("__DATA,__armkeep"))) \
    static void *armcave_unique(armcave_cpp_keep_) = (void *)&handler

#define new_cpp_func(handler, ...) \
    armcave_new_cpp_func(handler, __VA_ARGS__)

#define bind_func_by_sym(ret, name, args, symbol) \
    extern ret name args asm(symbol)

#define bind_func_by_addr(ret, name, args, addr) \
    extern ret name args asm("__armcave_va_" armcave_str(addr))

#define bind_obj_by_sym(type, name, symbol) \
    extern type name asm(symbol)

template <typename T>
static inline T read_mem(uintptr_t addr) {
    return *(volatile T *)addr;
}

template <typename T>
static inline void write_mem(uintptr_t addr, T value) {
    *(volatile T *)addr = value;
}


static inline uintptr_t resolve_vfunc(uintptr_t obj, uintptr_t offset) {
    uintptr_t vt = read_mem<uintptr_t>(obj);
    return vt ? read_mem<uintptr_t>(vt + offset) : 0;
}

static inline uintptr_t read_typeinfo(uintptr_t obj) {
    uintptr_t vt = read_mem<uintptr_t>(obj);
    return vt ? read_mem<uintptr_t>(vt - sizeof(uintptr_t)) : 0;
}

#define armcave_resolve_addr_impl(va, id) \
    ({ \
        extern char id asm("__armcave_data_" armcave_str(va)); \
        (uintptr_t)&id; \
    })
#define resolve_addr(va) armcave_resolve_addr_impl(va, armcave_unique(armcave_data_))
