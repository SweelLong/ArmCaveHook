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

__attribute__((always_inline))
static inline void armcave_sys_write(const char *p, int len) {
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
    0xfd, 0x7b, 0xbf, 0xa9,
    0xfd, 0x7b, 0xc1, 0xa8,
    0xc0, 0x03, 0x5f, 0xd6,
};

#define armcave_str2(x) #x
#define armcave_str(x) armcave_str2(x)
#define armcave_cat2(a, b) a##b
#define armcave_cat(a, b) armcave_cat2(a, b)
#define armcave_unique(prefix) armcave_cat(prefix, __COUNTER__)

struct armcave_target_ref {
    u8 opaque;
};

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

#define hook(addr, handler, ...) \
    armcave_meta("hook", addr, handler, auto, __VA_ARGS__)

#define cave(handler, ...) \
    armcave_meta("cave", 0, handler, auto, __VA_ARGS__)

#define inject_hex(addr, hex_bytes) \
    armcave_patch_meta("hex", addr, 0, hex_bytes, auto)

#define inject_asm(addr, asm_text) \
    armcave_patch_meta("asm", addr, 0, asm_text, auto)

#define target_fn(ret, name, args, symbol) \
    extern ret name args asm(symbol)

#define armcave_target_obj2(name, symbol) \
    extern armcave_target_ref name asm(symbol)

#define armcave_target_obj3(type, name, symbol) \
    extern type name asm(symbol)

#define armcave_pick_target_obj(_1, _2, _3, name, ...) name

#define target_obj(...) \
    armcave_pick_target_obj(__VA_ARGS__, armcave_target_obj3, armcave_target_obj2)(__VA_ARGS__)

#define target_obj_fn(name, symbol, ...) \
    extern armcave_target_ref *name(armcave_target_ref *, ##__VA_ARGS__) asm(symbol)

#define target_obj_call(fn, obj, ...) \
    fn(&obj, ##__VA_ARGS__)

#define target_call(ret, addr, args, ...) \
    ((ret (*) args)(addr))(__VA_ARGS__)

// ARMCAVE_BASE: default load address for arm64 Mach-O binaries
#ifndef ARMCAVE_BASE
#define ARMCAVE_BASE 0x100000000ULL
#endif

// Apple libc++ std::string layout on ARM64: 24 bytes, SSO up to 22 chars
struct StdString { char d[24]; };

// Call vtable[index] returning StdString (ARM64 sret via x8).
// Args are implicitly cast to void*. For zero extra args, omit the arg.
#define vt_call(obj, idx, arg) \
    ((StdString (*)(void *, void*))(*(void ***)(obj))[idx])((obj), (void*)(arg))

// Call a function at a file offset (ARMCAVE_BASE + offset) instead of a VA.
#define target_call_offset(ret, offset, args, ...) \
    ((ret (*) args)(ARMCAVE_BASE + (addr_t)(offset)))(__VA_ARGS__)

#ifdef __cplusplus
template <typename T, unsigned long cap = 32>
class vector {
    T items[cap];
    unsigned long count;
public:
    vector() : count(0) {}
    unsigned long size() const { return count; }
    unsigned long capacity() const { return cap; }
    bool empty() const { return count == 0; }
    T *data() { return items; }
    const T *data() const { return items; }
    T *begin() { return items; }
    T *end() { return items + count; }
    const T *begin() const { return items; }
    const T *end() const { return items + count; }
    T &operator[](unsigned long i) { return items[i]; }
    const T &operator[](unsigned long i) const { return items[i]; }
    T &front() { return items[0]; }
    T &back() { return items[count - 1]; }
    void clear() { count = 0; }
    void push_back(const T &v) { if (count < cap) items[count++] = v; }
    void pop_back() { if (count) --count; }
};

class string {
    char buf[128];
    unsigned long count;
public:
    string() : count(0) { buf[0] = 0; }
    string(const char *s) : count(0) { assign(s); }
    unsigned long size() const { return count; }
    unsigned long capacity() const { return sizeof(buf) - 1; }
    bool empty() const { return count == 0; }
    const char *c_str() const { return buf; }
    const char *data() const { return buf; }
    char *data() { return buf; }
    char &operator[](unsigned long i) { return buf[i]; }
    const char &operator[](unsigned long i) const { return buf[i]; }
    void clear() { count = 0; buf[0] = 0; }
    void push_back(char c) { if (count + 1 < sizeof(buf)) { buf[count++] = c; buf[count] = 0; } }
    void assign(const char *s) { clear(); append(s); }
    void append(const char *s) { while (*s) push_back(*s++); }
    string &operator+=(const char *s) { append(s); return *this; }
    string &operator+=(char c) { push_back(c); return *this; }
};
#endif
