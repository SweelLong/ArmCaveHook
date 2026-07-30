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

struct armcave_apple_string {
    char bytes[24];
    enum { kMetadataOffset = sizeof(bytes) - 1 };
};

static inline armcave_apple_string armcave_apple_string_make(const char *text) {
    armcave_apple_string value = {};
    unsigned long length = 0;
    while (length < armcave_apple_string::kMetadataOffset - 1 && text[length]) {
        value.bytes[length] = text[length];
        ++length;
    }
    value.bytes[armcave_apple_string::kMetadataOffset] = (char)length;
    return value;
}

static inline const char *armcave_apple_string_data(const armcave_apple_string &value) {
    return (value.bytes[armcave_apple_string::kMetadataOffset] & 0x80)
               ? *(const char *const *)value.bytes
               : value.bytes;
}

static inline armcave_apple_string armcave_apple_file_manager_get(void *manager, void *path) {
    typedef armcave_apple_string (*Method)(void *, void *);
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
    // libcocos2dcpp.so already imports this symbol from liblog, so the plugin
    // can call its existing PLT entry without adding a new ELF dependency.
    // Do not pass variadic arguments here: plugins are compiled with Darwin's
    // AArch64 ABI, whose variadic calling convention differs from Android's.
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
    buf[pos] = '\0';
    return pos;
}

static inline bool armcave_json_value(const char *json, int key, char *out, unsigned long out_size) {
    if (!json || !out || out_size == 0) return false;

    char key_buf[16];
    armcave_itoa(key_buf, key);
    for (const char *p = json; *p; ++p) {
        while (*p && *p++ != '"') {}
        if (!*p) break;
        const char *a = key_buf;
        const char *b = p;
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        if (!*a && *b == '"') {
            p = b + 1;
            while (*p && *p++ != '"') {}
            if (!*p) break;
            const char *value = p;
            while (*p && *p != '"') ++p;
            unsigned long length = 0;
            for (const char *c = value; c < p && length + 1 < out_size; ++length)
                out[length] = *c++;
            out[length] = 0;
            return true;
        }
    }
    return false;
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

#define armcave_patch_meta(kind, addr, size, payload, segment) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_patch_meta_)[] = \
        kind "|addr=" armcave_str(addr) "|size=" armcave_str(size) \
        "|data=" payload "|segment=" #segment

#define hook_replace(addr, handler, ...) \
    armcave_meta("hook_replace", addr, handler, auto, __VA_ARGS__)

#define hook_detour(addr, handler, ...) \
    armcave_meta("hook_detour", addr, handler, auto, __VA_ARGS__)

#define armcave_patch_asm(addr, asm_text) \
    armcave_patch_meta("patch_asm", addr, 0, asm_text, auto)

#define armcave_patch_meta_expected(kind, addr, expected, payload, segment) \
    __attribute__((used, section("__DATA,__armhook"))) \
    static const char armcave_unique(armcave_patch_meta_expected_)[] = \
        kind "|addr=" armcave_str(addr) "|expected=" armcave_str(expected) \
        "|size=0|data=" payload "|segment=" #segment

#define armcave_patch_asm_expected(addr, asm_text, expected) \
    armcave_patch_meta_expected("patch_asm", addr, expected, asm_text, auto)

#define armcave_pick_patch_asm(_1, _2, _3, name, ...) name
#define patch_asm(...) \
    armcave_pick_patch_asm(__VA_ARGS__, armcave_patch_asm_expected, armcave_patch_asm)(__VA_ARGS__)

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
