#pragma once

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long i64;
typedef unsigned long addr_t;

#include "armcave_sdk.h"

struct armcave_string {
    char bytes[24];
#ifndef ARMCAVE_ELF
    enum { kMetadataOffset = sizeof(bytes) - 1 };
#endif
};

static inline armcave_string armcave_string_make(const char *text) {
    armcave_string value = {};
    if (!text)
        return value;
    unsigned long length = 0;
#ifdef ARMCAVE_ELF
    while (length < sizeof(value.bytes) - 2 && text[length]) {
        value.bytes[length + 1] = text[length];
        ++length;
    }
    value.bytes[0] = (char)(length << 1);
#else
    while (length < armcave_string::kMetadataOffset - 1 && text[length]) {
        value.bytes[length] = text[length];
        ++length;
    }
    value.bytes[armcave_string::kMetadataOffset] = (char)length;
#endif
    return value;
}

static inline const char *armcave_string_data(const armcave_string &value) {
#ifdef ARMCAVE_ELF
    return (value.bytes[0] & 1)
               ? *(const char *const *)(value.bytes + 16)
               : value.bytes + 1;
#else
    return (value.bytes[armcave_string::kMetadataOffset] & 0x80)
               ? *(const char *const *)value.bytes
               : value.bytes;
#endif
}

static inline unsigned long armcave_string_size(const armcave_string &value) {
#ifdef ARMCAVE_ELF
    return (value.bytes[0] & 1)
               ? *(const unsigned long *)(value.bytes + 8)
               : (u8)value.bytes[0] >> 1;
#else
    i8 metadata = (i8)value.bytes[armcave_string::kMetadataOffset];
    return metadata < 0
               ? *(const unsigned long *)(value.bytes + sizeof(addr_t))
               : (u8)metadata;
#endif
}

typedef void (*armcave_string_release_fn)(void *);

static inline void armcave_string_destroy(
    armcave_string &value, armcave_string_release_fn release)
{
#ifdef ARMCAVE_ELF
    if ((value.bytes[0] & 1) && release)
        release(*(void **)(value.bytes + 16));
#else
    if ((i8)value.bytes[armcave_string::kMetadataOffset] < 0 && release)
        release(*(void **)value.bytes);
#endif
    value = {};
}

static inline armcave_string armcave_file_manager_get(void *manager, void *path) {
    typedef armcave_string (*Method)(void *, void *);
    Method method = (Method)(*(void ***)manager)[5];
    return method(manager, path);
}

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

#define armcave_patch_meta_signature(kind, signature, payload, segment) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_patch_meta_signature_)[] = \
        kind "|signature=" signature "|size=0|data=" payload \
        "|segment=" #segment

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
        kind "|addr=" armcave_str(addr) "|expected=" armcave_str(expected) \
        "|size=0|data=" payload "|segment=" #segment

#define armcave_patch_meta_signature_expected(kind, signature, expected, payload, segment) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_patch_meta_signature_expected_)[] = \
        kind "|signature=" signature "|expected=" armcave_str(expected) \
        "|size=0|data=" payload "|segment=" #segment

#define armcave_patch_asm_expected(addr, asm_text, expected) \
    armcave_patch_meta_expected("patch_asm", addr, expected, asm_text, auto)

#define armcave_patch_asm_signature(signature, asm_text) \
    armcave_patch_meta_signature("patch_asm", signature, asm_text, auto)

#define armcave_patch_asm_signature_expected(signature, asm_text, expected) \
    armcave_patch_meta_signature_expected("patch_asm", signature, expected, asm_text, auto)

#define armcave_pick_patch_asm(_1, _2, _3, name, ...) name
#define patch_asm(...) \
    armcave_pick_patch_asm(__VA_ARGS__, armcave_patch_asm_expected, armcave_patch_asm)(__VA_ARGS__)

#define armcave_patch_asm_func(addr, id) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_patch_asm_func_meta_)[] = \
        "patch_asm_func|addr=" armcave_str(addr) "|id=" armcave_str(id) \
        "|size=4|segment=auto"

#define patch_asm_func(addr, id) armcave_patch_asm_func(addr, id)

#define armcave_pick_patch_signature(_1, _2, _3, name, ...) name
#define patch_asm_signature(...) \
    armcave_pick_patch_signature(__VA_ARGS__, \
                                 armcave_patch_asm_signature_expected, \
                                 armcave_patch_asm_signature)(__VA_ARGS__)

#define new_asm_func_id(id, ...) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_new_asm_meta_)[] = \
        "new_asm_func|id=" armcave_str(id) "|args=" \
        armcave_str_args(__VA_ARGS__)

#define new_asm_func(...) \
    new_asm_func_id(__COUNTER__, __VA_ARGS__)

#define armcave_new_cpp_func_id(id, handler) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_new_cpp_meta_)[] = \
        "new_cpp_func|id=" armcave_str(id) "|handler=" #handler; \
    __attribute__((used, section("__DATA,__armkeep"))) \
    static void *armcave_unique(armcave_cpp_keep_) = (void *)&handler

#define new_cpp_func_id(id, handler) \
    armcave_new_cpp_func_id(id, handler)

#define new_cpp_func(handler) \
    new_cpp_func_id(__COUNTER__, handler)

#define bind_func_by_sym(ret, name, args, symbol) \
    extern ret name args asm(symbol)

#define bind_func_by_addr(ret, name, args, addr) \
    extern ret name args asm("__armcave_va_" armcave_str(addr))

#define bind_obj_by_sym(type, name, symbol) \
    extern type name asm(symbol)

template <typename T>
static inline T read_mem(addr_t addr) {
    return *(volatile T *)addr;
}

template <typename T>
static inline void write_mem(addr_t addr, T value) {
    *(volatile T *)addr = value;
}

static inline int armcave_timer_now_ms(
    addr_t gameplay,
    addr_t gameplay_timer_offset,
    addr_t timer_flag_offset,
    addr_t timer_ms_a_offset,
    addr_t timer_ms_b_offset,
    addr_t timer_ms_c_offset,
    int missing_value,
    int nonpositive_adjustment,
    bool clamp_minimum,
    int minimum_value) {
    if (!gameplay)
        return missing_value;
    addr_t timer = read_mem<addr_t>(gameplay + gameplay_timer_offset);
    if (!timer)
        return missing_value;
    if (read_mem<u8>(timer + timer_flag_offset) != 0)
        return read_mem<i32>(timer + timer_ms_a_offset) -
               read_mem<i32>(timer + timer_ms_b_offset);
    int timer_ms_c = read_mem<i32>(timer + timer_ms_c_offset);
    int value = timer_ms_c - read_mem<i32>(timer + timer_ms_b_offset);
    if (nonpositive_adjustment != 0 && timer_ms_c <= 0)
        value += nonpositive_adjustment;
    if (clamp_minimum && value < minimum_value)
        value = minimum_value;
    return value;
}

static inline addr_t resolve_vfunc(addr_t obj, addr_t offset) {
    addr_t vt = read_mem<addr_t>(obj);
    return vt ? read_mem<addr_t>(vt + offset) : 0;
}

static inline addr_t read_typeinfo(addr_t obj) {
    addr_t vt = read_mem<addr_t>(obj);
    return vt ? read_mem<addr_t>(vt - sizeof(addr_t)) : 0;
}

#define armcave_resolve_addr_impl(va, id) \
    ({ \
        extern char id asm("__armcave_data_" armcave_str(va)); \
        (addr_t)&id; \
    })
#define resolve_addr(va) armcave_resolve_addr_impl(va, armcave_unique(armcave_data_))
