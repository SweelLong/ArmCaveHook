#include "patcher.h"
#include "compiler.h"
#include "segment.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

static const uint8_t NOP[4] = {0x1f, 0x20, 0x03, 0xd5};
static const uint8_t DST[4] = {0xbe, 0xba, 0xfe, 0xca};
static const uint8_t NEXT[4] = {0xfe, 0xca, 0xad, 0xde};
static const uint8_t CONV[4] = {0xfe, 0xca, 0xef, 0xbe};

uint32_t encode_b(uint64_t src_va, uint64_t dst_va) {
    int64_t delta = (int64_t)(dst_va - src_va);
    if (delta & 3)
        throw std::runtime_error("branch target must be 4-byte aligned");
    int32_t imm = (int32_t)(delta >> 2);
    if (imm < -(1 << 25) || imm >= (1 << 25))
        throw std::runtime_error("branch target out of range");
    return 0x14000000 | (imm & 0x03FFFFFF);
}

uint32_t encode_bl(uint64_t src_va, uint64_t dst_va) {
    int64_t delta = (int64_t)(dst_va - src_va);
    if (delta & 3)
        throw std::runtime_error("branch target must be 4-byte aligned");
    int32_t imm = (int32_t)(delta >> 2);
    if (imm < -(1 << 25) || imm >= (1 << 25))
        throw std::runtime_error("branch target out of range");
    return 0x94000000 | (imm & 0x03FFFFFF);
}

uint64_t target_insn(uint32_t insn, uint64_t va) {
    if ((insn & 0xFC000000) == 0x14000000 || (insn & 0xFC000000) == 0x94000000) {
        int32_t imm = insn & 0x3FFFFFF;
        if (imm & 0x2000000) imm -= 0x4000000;
        return va + (int64_t)imm * 4;
    }
    if ((insn & 0xFF000010) == 0x54000000 || (insn & 0x7E000000) == 0x34000000) {
        int32_t imm = (insn >> 5) & 0x7FFFF;
        if (imm & 0x40000) imm -= 0x80000;
        return va + (int64_t)imm * 4;
    }
    if ((insn & 0x7E000000) == 0x36000000) {
        int32_t imm = (insn >> 19) & 0x3FFF;
        if (imm & 0x2000) imm -= 0x4000;
        return va + (int64_t)imm * 4;
    }
    return 0;
}

static std::vector<uint8_t> patched(const std::vector<uint8_t> &original,
                                     uint64_t hook_va, uint64_t cave_va) {
    std::vector<uint8_t> out;
    uint64_t cur = cave_va;
    for (size_t i = 0; i < original.size(); i += 4) {
        uint32_t raw;
        memcpy(&raw, original.data() + i, 4);
        uint64_t va = hook_va + i;
        if ((raw & 0xFC000000) == 0x14000000) {
            auto b = encode_b(cur, target_insn(raw, va));
            auto bytes = (const uint8_t *)&b;
            out.insert(out.end(), bytes, bytes + 4);
            cur += 4;
        } else if ((raw & 0xFC000000) == 0x94000000) {
            auto bl = encode_bl(cur, target_insn(raw, va));
            auto bytes = (const uint8_t *)&bl;
            out.insert(out.end(), bytes, bytes + 4);
            cur += 4;
        } else {
            out.insert(out.end(), original.begin() + i, original.begin() + i + 4);
            cur += 4;
        }
    }
    return out;
}

static int patched_size(const std::vector<uint8_t> &original) {
    return (int)original.size();
}

static int reg_num(const std::string &name) {
    return atoi(name.c_str() + 1);
}

static bool reg_is64(const std::string &name) {
    return name[0] == 'x' || name[0] == 'X';
}

static std::vector<uint8_t> mov_insn(int rd, int rm, bool is64) {
    uint32_t insn = (is64 ? 0xAA0003E0 : 0x2A0003E0) | (rm << 16) | rd;
    std::vector<uint8_t> out(4);
    memcpy(out.data(), &insn, 4);
    return out;
}

static std::vector<uint8_t> make_wrapper(const std::vector<std::string> &regs,
                                          uint64_t wrapper_va, uint64_t plugin_va) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < regs.size() && i < 8; i++) {
        auto m = mov_insn((int)i, reg_num(regs[i]), reg_is64(regs[i]));
        out.insert(out.end(), m.begin(), m.end());
    }
    auto b = encode_b(wrapper_va + out.size(), plugin_va);
    uint8_t buf[4];
    memcpy(buf, &b, 4);
    out.insert(out.end(), buf, buf + 4);
    return out;
}

static struct CaveFrame {
    std::vector<uint8_t> save;
    std::vector<uint8_t> restore;
    std::vector<uint8_t> ret;
    bool valid = false;
} g_cave_frame;

static void ensure_cave_frame() {
    if (g_cave_frame.valid) return;
    g_cave_frame.save = extract_cave_asm_save();
    g_cave_frame.restore = extract_cave_asm_restore();
    g_cave_frame.ret = extract_cave_asm_ret();
    g_cave_frame.valid = true;
}

int hook_dispatch_size(int handler_count, int original_size, bool override_original,
                       bool strip_pac) {
    ensure_cave_frame();
    int save_size = (int)g_cave_frame.save.size() - (strip_pac ? 0 : 4);
    return save_size + handler_count * 4 +
           (int)g_cave_frame.restore.size() +
           (override_original ? (int)g_cave_frame.ret.size() : original_size + 4);
}

std::vector<uint8_t> build_hook_dispatch(
    uint64_t cave_va,
    uint64_t hook_va,
    int hook_size,
    const std::vector<uint8_t> &original,
    const std::vector<uint64_t> &handler_vas,
    bool override_original,
    bool strip_pac) {

    if (original.empty() || original.size() % 4 || hook_size % 4)
        throw std::runtime_error("invalid hook window");
    ensure_cave_frame();

    size_t save_offset = strip_pac ? 0 : 4;
    if (g_cave_frame.save.size() < save_offset)
        throw std::runtime_error("invalid cave save sequence");
    uint64_t calls_va = cave_va + g_cave_frame.save.size() - save_offset;
    uint64_t resume_va = calls_va + handler_vas.size() * 4 + g_cave_frame.restore.size();
    std::vector<uint8_t> relocated;
    if (!override_original)
        relocated = patched(original, hook_va, resume_va);

    std::vector<uint8_t> out;
    out.insert(out.end(), g_cave_frame.save.begin() + save_offset,
               g_cave_frame.save.end());
    for (size_t i = 0; i < handler_vas.size(); ++i) {
        auto bl = encode_bl(calls_va + i * 4, handler_vas[i]);
        auto *bytes = (const uint8_t *)&bl;
        out.insert(out.end(), bytes, bytes + 4);
    }
    out.insert(out.end(), g_cave_frame.restore.begin(), g_cave_frame.restore.end());
    if (override_original) {
        out.insert(out.end(), g_cave_frame.ret.begin(), g_cave_frame.ret.end());
    } else {
        out.insert(out.end(), relocated.begin(), relocated.end());
        auto branch = encode_b(cave_va + out.size(), hook_va + hook_size);
        auto *bytes = (const uint8_t *)&branch;
        out.insert(out.end(), bytes, bytes + 4);
    }
    return out;
}

int plugin_wrapper_size(const std::vector<std::string> &registers) {
    return registers.empty() ? 0 : (int)std::min<size_t>(registers.size(), 8) * 4 + 4;
}

std::vector<uint8_t> build_plugin_wrapper(
    const std::vector<std::string> &registers,
    uint64_t wrapper_va,
    uint64_t plugin_va) {
    return make_wrapper(registers, wrapper_va, plugin_va);
}

std::vector<uint8_t> build_hook_cave(
    uint64_t cave_va, uint64_t hook_va, int hook_size,
    const std::vector<uint8_t> &original,
    const std::vector<PluginBlob *> &plugin_blobs,
    const std::filesystem::path *target_binary,
    bool override_original, bool branch_host,
    const std::vector<uint64_t> *nop_addrs) {

    (void)nop_addrs;

    if (original.empty() || original.size() % 4 || hook_size % 4)
        throw std::runtime_error("invalid hook window");

    ensure_cave_frame();

    int n = (int)plugin_blobs.size();
    uint64_t target_dst_val = 0;

    if (branch_host) {
        override_original = true;
        for (size_t i = 0; i < original.size(); i += 4) {
            uint32_t insn;
            memcpy(&insn, original.data() + i, 4);
            uint64_t tgt = target_insn(insn, hook_va + i);
            if (tgt) {
                target_dst_val = tgt;
                break;
            }
        }
        if (!target_dst_val)
            throw std::runtime_error("branch hook needs a branch instruction");
    }

    uint64_t calls_va = cave_va + g_cave_frame.save.size();
    uint64_t resume_va = calls_va + n * 4 + g_cave_frame.restore.size();

    std::vector<uint8_t> patched_original;
    if (!override_original)
        patched_original = patched(original, hook_va, resume_va);

    int control_size = (int)g_cave_frame.save.size() + n * 4 + (int)g_cave_frame.restore.size()
                       + (override_original ? (int)g_cave_frame.ret.size() : (int)patched_original.size() + 4);

    std::vector<int> offsets;
    std::vector<std::vector<uint8_t>> chunks;
    int cur = control_size;

    for (int i = 0; i < n; i++) {
        auto *blob = plugin_blobs[i];
        cur = (cur + 3) & ~3;
        auto &regs = blob->register_args;
        int entry = blob->entry_offset;

        if (!regs.empty()) {
            int woff = cur;
            cur += (int)(regs.size() * 4 + 4);
            cur = (cur + 3) & ~3;
            int poff = cur;
            offsets.push_back(woff);

            auto built = blob->build(cave_va + poff,
                cave_va + poff + ((int)blob->text.size() + 15 & ~15),
                target_binary);
            chunks.push_back(make_wrapper(regs, cave_va + woff, cave_va + poff + entry));
            chunks.push_back(built);
            cur = poff + (int)built.size();
        } else {
            offsets.push_back(cur + entry);
            auto built = blob->build(cave_va + cur,
                cave_va + cur + ((int)blob->text.size() + 15 & ~15),
                target_binary);
            chunks.push_back(built);
            cur += (int)built.size();
        }
    }

    std::vector<uint8_t> out;
    out.insert(out.end(), g_cave_frame.save.begin(), g_cave_frame.save.end());

    for (int i = 0; i < n; i++) {
        auto bl = encode_bl(calls_va + i * 4, cave_va + offsets[i]);
        uint8_t buf[4];
        memcpy(buf, &bl, 4);
        out.insert(out.end(), buf, buf + 4);
    }

    out.insert(out.end(), g_cave_frame.restore.begin(), g_cave_frame.restore.end());

    if (override_original) {
        out.insert(out.end(), g_cave_frame.ret.begin(), g_cave_frame.ret.end());
    } else {
        out.insert(out.end(), patched_original.begin(), patched_original.end());
        auto b = encode_b(resume_va + patched_original.size(), hook_va + hook_size);
        uint8_t buf[4];
        memcpy(buf, &b, 4);
        out.insert(out.end(), buf, buf + 4);
    }

    for (auto &chunk : chunks) {
        out.insert(out.end(), chunk.begin(), chunk.end());
        while (out.size() % 4 != 0) out.push_back(0);
    }

    if (target_dst_val) {
        struct { const uint8_t *marker; uint64_t target; } markers[] = {
            {DST, target_dst_val},
            {NEXT, hook_va + 4},
            {CONV, hook_va + (uint64_t)hook_size}
        };
        for (auto &m : markers) {
            size_t idx = 0;
            while (true) {
                auto pos = out.begin() + (int)idx;
                auto found = std::search(pos, out.end(), m.marker, m.marker + 4);
                if (found == out.end()) break;
                int off = (int)(found - out.begin());
                auto b = encode_b(cave_va + off, m.target);
                memcpy(&out[off], &b, 4);
                idx = off + 1;
            }
        }
    }

    return out;
}

static void codesign(const std::filesystem::path &path) {
#ifdef __APPLE__
    std::string quoted = "'";
    for (char c : path.string()) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += '\'';
    std::string cmd = "codesign --force --sign - " + quoted + " >/dev/null 2>/dev/null";
    system(cmd.c_str());
#else
    (void)path;
#endif
}

static int va_to_offset(BinaryImage *binary, uint64_t va) {
    auto off = binary->virtual_address_to_offset(va);
    if (!off)
        throw std::runtime_error("cannot map VA");
    return (int)*off;
}

void patch_hook_window(const std::filesystem::path &binary_path,
                       const std::filesystem::path &output_path,
                       uint64_t src_va, int size, uint64_t dst_va) {
    (void)binary_path;
    auto binary = BinaryImage::parse(output_path);
    if (!binary)
        throw std::runtime_error("failed to parse " + output_path.string());

    int off = va_to_offset(binary.get(), src_va);
    uint32_t b = encode_b(src_va, dst_va);
    auto data = read_file(output_path);

    if ((int)sizeof(b) > size)
        throw std::runtime_error("hook window too small");

    memcpy(&data[off], &b, 4);
    for (int pos = 4; pos < size; pos += 4)
        memcpy(&data[off + pos], NOP, 4);

    write_file(output_path, data);
}

void patch_bytes_va(const std::filesystem::path &binary_path,
                    const std::filesystem::path &output_path,
                    uint64_t va, const std::vector<uint8_t> &payload) {
    (void)binary_path;
    auto binary = BinaryImage::parse(output_path);
    if (!binary)
        throw std::runtime_error("failed to parse " + output_path.string());

    int off = va_to_offset(binary.get(), va);
    auto data = read_file(output_path);
    if (off + (int)payload.size() > (int)data.size())
        throw std::runtime_error("patch out of range");
    memcpy(&data[off], payload.data(), payload.size());
    write_file(output_path, data);
}

std::pair<uint64_t, uint64_t> patch_hook_macho(
    const std::filesystem::path &binary_path,
    const std::filesystem::path &output_path,
    int hook_file_off, int hook_size,
    const std::vector<uint8_t> &original,
    const std::vector<PluginBlob *> &plugin_blobs,
    const std::string &seg_name_str,
    bool override_original, bool branch_host,
    const std::vector<uint64_t> *nop_addrs) {

    auto before = open_macho(binary_path.string());
    if (!before.bin)
        throw std::runtime_error("failed to parse " + binary_path.string());

    ensure_cave_frame();

    int n = (int)plugin_blobs.size();
    int control = (int)g_cave_frame.save.size() + n * 4 + (int)g_cave_frame.restore.size()
                  + (override_original ? (int)g_cave_frame.ret.size() : patched_size(original) + 4);
    int size = control;
    for (auto *b : plugin_blobs) {
        size += b->total_bytes();
        if (!b->register_args.empty())
            size += (int)(b->register_args.size() * 4 + 4);
    }
    if (size < 4) size = 4;

    SegmentPlan plan;
    plan.name = seg_name_str;
    plan.size = size;
    plan.content.resize(size, 0);
    add_segment(output_path, plan, output_path);

    auto after = open_macho(output_path.string());
    if (!after.bin)
        throw std::runtime_error("failed to parse " + output_path.string());

    uint64_t hook_va = remap_macho_offset_va(*before.bin, *after.bin, hook_file_off);
    uint64_t cave_va = seg_va(*after.bin, seg_name_str, size);
    int cave_off = segment_file_offset(*after.bin, seg_name_str);

    auto blob = build_hook_cave(cave_va, hook_va, hook_size, original, plugin_blobs,
                                 &output_path, override_original, branch_host, nop_addrs);

    if (size < (int)blob.size()) size = (int)blob.size();
    blob.resize(size, 0);

    write_at_offset(output_path, cave_off, blob, size);
    patch_hook_window(output_path, output_path, hook_va, hook_size, cave_va);
    codesign(output_path);

    return {hook_va, cave_va};
}
