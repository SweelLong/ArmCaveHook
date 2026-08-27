#include "patcher.h"
#include "aarch64/decoder.h"
#include "aarch64/encoder.h"
#include "aarch64/relocator.h"
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
    return armcave::aarch64::encode_b(src_va, dst_va);
}

uint32_t encode_bl(uint64_t src_va, uint64_t dst_va) {
    return armcave::aarch64::encode_bl(src_va, dst_va);
}

uint64_t target_insn(uint32_t insn, uint64_t va) {
    auto decoded = armcave::aarch64::decode(insn, va);
    return decoded.has_target && armcave::aarch64::is_branch(decoded.kind)
        ? decoded.target : 0;
}

static std::vector<uint8_t> patched(const std::vector<uint8_t> &original,
                                     uint64_t hook_va, uint64_t cave_va) {
    return armcave::aarch64::relocate_block(original, hook_va, cave_va).bytes;
}

static int patched_size(const std::vector<uint8_t> &original) {
    return (int)armcave::aarch64::max_relocated_size(original.size());
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
    auto branch = armcave::aarch64::make_branch_sequence(
        wrapper_va + out.size(), plugin_va, false);
    out.insert(out.end(), branch.begin(), branch.end());
    return out;
}

static void append_word(std::vector<uint8_t> &out, uint32_t instruction) {
    out.push_back((uint8_t)(instruction & 0xffU));
    out.push_back((uint8_t)((instruction >> 8) & 0xffU));
    out.push_back((uint8_t)((instruction >> 16) & 0xffU));
    out.push_back((uint8_t)((instruction >> 24) & 0xffU));
}

static std::vector<uint8_t> make_cpp_func_wrapper(
    const std::vector<std::string> &regs,
    uint64_t wrapper_va, uint64_t plugin_va) {
    (void)regs;
    std::vector<uint8_t> out;

    // The call site prepares the first C++ argument in w0. Preserve the
    // caller's LR, then expose the C++ return value in x1.
    append_word(out, 0xa9bf7bfdU); // stp x29, x30, [sp, #-16]!
    auto branch = armcave::aarch64::make_branch_sequence(
        wrapper_va + out.size(), plugin_va, true);
    out.insert(out.end(), branch.begin(), branch.end());
    append_word(out, 0xaa0003e1U); // mov x1, x0
    append_word(out, 0xa8c17bfdU); // ldp x29, x30, [sp], #16
    append_word(out, 0xd65f03c0U); // ret
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
    return save_size + handler_count * (int)armcave::aarch64::kMaxBranchSequenceBytes +
           (int)g_cave_frame.restore.size() +
           (override_original ? (int)g_cave_frame.ret.size() :
            (int)armcave::aarch64::max_relocated_size((size_t)original_size) +
            (int)armcave::aarch64::kMaxBranchSequenceBytes);
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
    std::vector<std::vector<uint8_t>> calls;
    uint64_t call_cursor = calls_va;
    for (auto handler_va : handler_vas)
    {
        auto call = armcave::aarch64::make_branch_sequence(
            call_cursor, handler_va, true);
        call_cursor += call.size();
        calls.push_back(std::move(call));
    }
    uint64_t resume_va = call_cursor + g_cave_frame.restore.size();
    std::vector<uint8_t> relocated;
    if (!override_original)
        relocated = patched(original, hook_va, resume_va);

    std::vector<uint8_t> out;
    out.insert(out.end(), g_cave_frame.save.begin() + save_offset,
               g_cave_frame.save.end());
    for (auto &call : calls)
        out.insert(out.end(), call.begin(), call.end());
    out.insert(out.end(), g_cave_frame.restore.begin(), g_cave_frame.restore.end());
    if (override_original) {
        out.insert(out.end(), g_cave_frame.ret.begin(), g_cave_frame.ret.end());
    } else {
        out.insert(out.end(), relocated.begin(), relocated.end());
        auto branch = armcave::aarch64::make_branch_sequence(
            cave_va + out.size(), hook_va + hook_size, false);
        out.insert(out.end(), branch.begin(), branch.end());
    }
    return out;
}

int plugin_wrapper_size(const std::vector<std::string> &registers) {
    return registers.empty() ? 0 : (int)std::min<size_t>(registers.size(), 8) * 4 + 4;
}

int plugin_wrapper_max_size(const std::vector<std::string> &registers) {
    return registers.empty() ? 0 : (int)std::min<size_t>(registers.size(), 8) * 4 +
                                      (int)armcave::aarch64::kMaxBranchSequenceBytes;
}

int cpp_func_wrapper_size(const std::vector<std::string> &registers) {
    return 4 + (int)std::min<size_t>(registers.size(), 8) * 4 + 4 + 12;
}

int cpp_func_wrapper_max_size(const std::vector<std::string> &registers) {
    return 4 + (int)std::min<size_t>(registers.size(), 8) * 4 +
           (int)armcave::aarch64::kMaxBranchSequenceBytes + 12;
}

std::vector<uint8_t> build_plugin_wrapper(
    const std::vector<std::string> &registers,
    uint64_t wrapper_va,
    uint64_t plugin_va,
    bool cpp_func) {
    return cpp_func ? make_cpp_func_wrapper(registers, wrapper_va, plugin_va)
                    : make_wrapper(registers, wrapper_va, plugin_va);
}

int hook_window_size(uint64_t src_va, uint64_t dst_va) {
    return (int)armcave::aarch64::branch_sequence_size(src_va, dst_va, false);
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
    const int call_slot_size = (int)armcave::aarch64::kMaxBranchSequenceBytes;
    const int save_size = (int)g_cave_frame.save.size();
    const int restore_size = (int)g_cave_frame.restore.size();
    const int control_size = save_size + n * call_slot_size + restore_size +
        (override_original ? (int)g_cave_frame.ret.size() :
         (int)armcave::aarch64::max_relocated_size(original.size()) +
         (int)armcave::aarch64::kMaxBranchSequenceBytes);
    uint64_t calls_va = cave_va + save_size;
    uint64_t resume_va = cave_va + save_size + n * call_slot_size + restore_size;
    std::vector<uint8_t> patched_original;
    if (!override_original)
        patched_original = patched(original, hook_va, resume_va);
    std::vector<int> offsets;
    struct Chunk { int offset; std::vector<uint8_t> bytes; };
    std::vector<Chunk> chunks;
    int cur = control_size;
    for (int i = 0; i < n; i++) {
        auto *blob = plugin_blobs[i];
        cur = (cur + 3) & ~3;
        auto &regs = blob->register_args;
        int entry = blob->entry_offset;
        if (!regs.empty()) {
            int woff = cur;
            cur += plugin_wrapper_max_size(regs);
            cur = (cur + 3) & ~3;
            int poff = cur;
            offsets.push_back(woff);
            int text_aligned = (blob->max_text_bytes() + 15) & ~15;
            auto built = blob->build(cave_va + poff,
                cave_va + poff + text_aligned,
                target_binary);
            chunks.push_back({woff, make_wrapper(regs, cave_va + woff,
                                                 cave_va + poff + entry)});
            int built_size = (int)built.size();
            chunks.push_back({poff, std::move(built)});
            cur = poff + built_size;
        } else {
            offsets.push_back(cur + entry);
            int text_aligned = (blob->max_text_bytes() + 15) & ~15;
            auto built = blob->build(cave_va + cur,
                cave_va + cur + text_aligned,
                target_binary);
            int built_size = (int)built.size();
            chunks.push_back({cur, std::move(built)});
            cur += built_size;
        }
    }
    std::vector<uint8_t> out;
    out.insert(out.end(), g_cave_frame.save.begin(), g_cave_frame.save.end());
    for (int i = 0; i < n; i++) {
        auto call = armcave::aarch64::make_branch_sequence(
            calls_va + (uint64_t)i * call_slot_size,
            cave_va + offsets[i], true);
        out.insert(out.end(), call.begin(), call.end());
        while (out.size() < save_size + (size_t)(i + 1) * call_slot_size)
            out.insert(out.end(), NOP, NOP + 4);
    }
    out.insert(out.end(), g_cave_frame.restore.begin(), g_cave_frame.restore.end());
    if (override_original) {
        out.insert(out.end(), g_cave_frame.ret.begin(), g_cave_frame.ret.end());
    } else {
        out.insert(out.end(), patched_original.begin(), patched_original.end());
        auto branch = armcave::aarch64::make_branch_sequence(
            cave_va + out.size(), hook_va + hook_size, false);
        out.insert(out.end(), branch.begin(), branch.end());
    }
    if ((int)out.size() > control_size)
        throw std::runtime_error("hook dispatcher exceeded reserved size");
    out.resize((size_t)cur, 0);
    for (auto &chunk : chunks) {
        if (chunk.offset < 0 || (size_t)chunk.offset + chunk.bytes.size() > out.size())
            throw std::runtime_error("hook cave layout overflow");
        std::copy(chunk.bytes.begin(), chunk.bytes.end(), out.begin() + chunk.offset);
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
                auto branch = armcave::aarch64::make_branch_sequence(
                    cave_va + off, m.target, false);
                if (branch.size() != 4)
                    throw std::runtime_error("branch host marker is too small for far target");
                memcpy(&out[off], branch.data(), branch.size());
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
    auto branch = armcave::aarch64::make_branch_sequence(src_va, dst_va, false);
    auto data = read_file(output_path);

    if ((int)branch.size() > size || size % 4)
        throw std::runtime_error("hook window too small");

    if (off < 0 || (size_t)off + (size_t)size > data.size())
        throw std::runtime_error("hook window is outside the file");
    memcpy(&data[off], branch.data(), branch.size());
    for (int pos = (int)branch.size(); pos < size; pos += 4)
        memcpy(&data[off + pos], NOP, 4);

    write_file(output_path, data);
}

void patch_call_window(const std::filesystem::path &binary_path,
                       const std::filesystem::path &output_path,
                       uint64_t src_va, int size, uint64_t dst_va) {
    (void)binary_path;
    auto binary = BinaryImage::parse(output_path);
    if (!binary)
        throw std::runtime_error("failed to parse " + output_path.string());

    int off = va_to_offset(binary.get(), src_va);
    auto call = armcave::aarch64::make_branch_sequence(src_va, dst_va, true);
    auto data = read_file(output_path);
    if ((int)call.size() > size || size % 4)
        throw std::runtime_error("call window too small");
    if (off < 0 || (size_t)off + (size_t)size > data.size())
        throw std::runtime_error("call window is outside the file");
    memcpy(&data[off], call.data(), call.size());
    for (int pos = (int)call.size(); pos < size; pos += 4)
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
    int control = (int)g_cave_frame.save.size() +
                  n * (int)armcave::aarch64::kMaxBranchSequenceBytes +
                  (int)g_cave_frame.restore.size() +
                  (override_original ? (int)g_cave_frame.ret.size() :
                   patched_size(original) +
                   (int)armcave::aarch64::kMaxBranchSequenceBytes);
    int size = control;
    for (auto *b : plugin_blobs) {
        size += b->total_bytes();
        size += plugin_wrapper_max_size(b->register_args) + 16;
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
    int actual_hook_size = hook_window_size(hook_va, cave_va);
    if (actual_hook_size > (int)original.size())
        throw std::runtime_error("Mach-O hook window is too small for far trampoline");
    hook_size = std::max(hook_size, actual_hook_size);

    auto blob = build_hook_cave(cave_va, hook_va, hook_size, original, plugin_blobs,
                                 &output_path, override_original, branch_host, nop_addrs);

    if (size < (int)blob.size())
        throw std::runtime_error("Mach-O cave exceeded reserved segment");
    blob.resize(size, 0);

    write_at_offset(output_path, cave_off, blob, size);
    patch_hook_window(output_path, output_path, hook_va, hook_size, cave_va);
    codesign(output_path);

    return {hook_va, cave_va};
}
