#include "pipeline.h"
#include "compiler.h"
#include "patcher.h"
#include "plugin.h"
#include "segment.h"
#include "signature.h"
#include "diagnostic.h"
#include "apple_metadata.h"
#include "symbols.h"
#include "patch_script.h"
#include "aarch64/encoder.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <regex>
#include <map>
#include <set>
#include <memory>
#include <array>
#include <cstdint>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>

static constexpr int kMaxHookWindow = 20;

static std::string sha1_hex3(const std::string &input) {
    auto rol = [](uint32_t value, int bits) {
        return (value << bits) | (value >> (32 - bits));
    };
    std::vector<uint8_t> data(input.begin(), input.end());
    uint64_t bit_length = (uint64_t)data.size() * 8;
    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8)
        data.push_back((uint8_t)(bit_length >> shift));

    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    for (size_t offset = 0; offset < data.size(); offset += 64) {
        std::array<uint32_t, 80> words{};
        for (int i = 0; i < 16; ++i) {
            size_t p = offset + (size_t)i * 4;
            words[i] = ((uint32_t)data[p] << 24) | ((uint32_t)data[p + 1] << 16) |
                       ((uint32_t)data[p + 2] << 8) | data[p + 3];
        }
        for (int i = 16; i < 80; ++i)
            words[i] = rol(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t next = rol(a, 5) + f + e + k + words[i];
            e = d; d = c; c = rol(b, 30); b = a; a = next;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    uint8_t digest[20];
    uint32_t state[] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j)
            digest[i * 4 + j] = (uint8_t)(state[i] >> (24 - j * 8));
    char buf[8];
    snprintf(buf, sizeof(buf), "%02x%02x%02x", digest[0], digest[1], digest[2]);
    return std::string(buf, 3);
}

static bool is_macho(const std::filesystem::path &path) {
    auto binary = BinaryImage::parse(path);
    return binary && binary->is_macho();
}

#ifdef __APPLE__
static std::string shell_quote(const std::string &value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += '\'';
    return out;
}
#endif

static BinaryImage &parse_binary(const std::filesystem::path &path) {
    static std::unique_ptr<BinaryImage> cached;
    cached = BinaryImage::parse(path);
    if (!cached) throw std::runtime_error("failed to parse " + path.string());
    return *cached;
}

static int file_offset(BinaryImage &binary, uint64_t va) {
    auto offset = binary.virtual_address_to_offset(va);
    if (!offset) throw std::runtime_error("cannot map VA");
    return (int)*offset;
}

static uint64_t read_entry(BinaryImage &binary) {
    return binary.entrypoint();
}

static std::string make_plugin_seg_name(const std::string &plugin,
                                        const std::string &prefix,
                                        std::set<std::string> &used) {
    std::string raw = prefix.empty() ? plugin : prefix;
    std::string base = std::regex_replace(raw, std::regex("[^A-Za-z0-9_]+"), "_");
    while (!base.empty() && base[0] == '_') base.erase(0, 1);
    while (!base.empty() && base.back() == '_') base.pop_back();
    std::transform(base.begin(), base.end(), base.begin(), ::tolower);
    if (base.empty()) base = "armcave";
    if (base.size() > 14) base.resize(14);
    if (!used.insert(base).second) {
        std::string suffix = "_" + sha1_hex3(plugin);
        if (base.size() + suffix.size() > 14)
            base.resize(14 - suffix.size());
        base += suffix;
        if (!used.insert(base).second)
            throw std::runtime_error("duplicate plugin segment name: " + base);
    }
    return base;
}

static std::string make_plugin_data_seg_name(const std::string &code_name,
                                             std::set<std::string> &used) {
    constexpr size_t kMaxSegmentName = 14;
    const std::string suffix = "_data";
    std::string base = code_name;
    if (base.size() + suffix.size() > kMaxSegmentName)
        base.resize(kMaxSegmentName - suffix.size());
    std::string candidate = base + suffix;
    if (used.insert(candidate).second)
        return candidate;

    std::string hash_suffix = "_d" + sha1_hex3(code_name);
    base = code_name.substr(0, kMaxSegmentName - hash_suffix.size());
    candidate = base + hash_suffix;
    if (!used.insert(candidate).second)
        throw std::runtime_error("duplicate plugin data segment name: " + candidate);
    return candidate;
}

static bool has_plugin_segment(BinaryImage &binary, const std::string &name) {
    std::string target = seg_name(binary, name);
    if (binary.is_macho()) {
        for (const auto &segment : binary.segments())
            if (segment.name == target) return true;
        return false;
    }
    return binary.section(target) != nullptr;
}

static std::string normalize_asm_text(std::string asm_text) {
    std::replace(asm_text.begin(), asm_text.end(), ';', '\n');
    size_t pos = 0;
    while ((pos = asm_text.find("\\n", pos)) != std::string::npos) {
        asm_text.replace(pos, 2, "\n");
        ++pos;
    }
    return asm_text;
}

static std::vector<uint8_t> parse_hex_payload(const std::string &text) {
    static const std::string separators = " \t\r\n,;";
    std::string digits;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '0' && i + 2 < text.size() &&
            (text[i + 1] == 'x' || text[i + 1] == 'X') &&
            isxdigit((unsigned char)text[i + 2])) {
            ++i; // skip the 'x' of a 0x prefix; the digits are collected below
            continue;
        }
        if (separators.find(c) != std::string::npos)
            continue;
        if (!isxdigit((unsigned char)c))
            throw std::runtime_error(std::string("patch_hex has an invalid character '") +
                                     c + "' in \"" + text + "\"");
        digits += c;
    }
    if (digits.empty() || digits.size() % 2 != 0)
        throw std::runtime_error("patch_hex needs an even number of hex digits in \"" +
                                 text + "\"");
    std::vector<uint8_t> out;
    out.reserve(digits.size() / 2);
    for (size_t i = 0; i < digits.size(); i += 2) {
        char pair[3] = {digits[i], digits[i + 1], 0};
        out.push_back((uint8_t)strtoul(pair, nullptr, 16));
    }
    return out;
}

static bool is_asm_label_char(char c) {
    return std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '$';
}

static bool contains_asm_label(const std::string &source, const std::string &label) {
    size_t pos = 0;
    while ((pos = source.find(label, pos)) != std::string::npos) {
        bool left_is_label = pos > 0 && is_asm_label_char(source[pos - 1]);
        size_t end = pos + label.size();
        bool right_is_label = end < source.size() && is_asm_label_char(source[end]);
        if (!left_is_label && !right_is_label)
            return true;
        pos = end;
    }
    return false;
}

static bool references_data_label(const HookAction &action,
                                  const PluginBlob &blob) {
    if (action.kind != "patch_asm")
        return false;
    for (const auto &entry : blob.data_symbol_offsets)
        if (!entry.first.empty() && contains_asm_label(action.data, entry.first))
            return true;
    return false;
}

static std::string normalize_registered_labels(
    std::string source, uint64_t address,
    const std::map<std::string, uint64_t> &label_targets) {
    if (!address || label_targets.empty())
        return source;

    static const std::regex branch_re(
        R"(^([ \t]*(?:b(?:\.[A-Za-z0-9]+)?|bl)[ \t]+)([A-Za-z_.$][A-Za-z0-9_.$]*)(.*)$)",
        std::regex::icase);
    static const std::regex adrl_re(
        R"(^([ \t]*)adrl[ \t]+(x[0-9]+)[ \t]*,[ \t]*#?[ \t]*([A-Za-z_.$][A-Za-z0-9_.$]*)(.*)$)",
        std::regex::icase);
    static const std::regex adrp_re(
        R"(^([ \t]*adrp[ \t]+x[0-9]+[ \t]*,[ \t]*#?[ \t]*)([A-Za-z_.$][A-Za-z0-9_.$]*)(.*)$)",
        std::regex::icase);
    static const std::regex add_re(
        R"(^([ \t]*add[ \t]+[xw][0-9]+[ \t]*,[ \t]*[xw][0-9]+[ \t]*,)[ \t]*#?[ \t]*(?::lo12:)?([A-Za-z_.$][A-Za-z0-9_.$]*)(.*)$)",
        std::regex::icase);
    static const std::regex load_re(
        R"(^([ \t]*(?:ldr|str)[ \t]+[xw][0-9]+[ \t]*,[ \t]*\[[ \t]*x[0-9]+[ \t]*,)[ \t]*(?::lo12:)?([A-Za-z_.$][A-Za-z0-9_.$]*)[ \t]*\](.*)$)",
        std::regex::icase);
    std::istringstream input(normalize_asm_text(source));
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, branch_re)) {
            auto target = label_targets.find(match[2].str());
            if (target != label_targets.end())
                line = match[1].str() + std::to_string(target->second) + match[3].str();
        } else if (std::regex_match(line, match, adrl_re)) {
            auto target = label_targets.find(match[3].str());
            if (target != label_targets.end()) {
                line = match[1].str() + "adrp " + match[2].str() + ", " +
                       std::to_string(target->second) + "\n" + match[1].str() +
                       "add " + match[2].str() + ", " + match[2].str() + ", #" +
                       std::to_string(target->second & 0xfffU) + match[4].str();
            }
        } else if (std::regex_match(line, match, adrp_re)) {
            auto target = label_targets.find(match[2].str());
            if (target != label_targets.end())
                line = match[1].str() + std::to_string(target->second) + match[3].str();
        } else if (std::regex_match(line, match, add_re)) {
            auto target = label_targets.find(match[2].str());
            if (target != label_targets.end())
                line = match[1].str() + " #" +
                       std::to_string(target->second & 0xfffU) + match[3].str();
        } else if (std::regex_match(line, match, load_re)) {
            auto target = label_targets.find(match[2].str());
            if (target != label_targets.end())
                line = match[1].str() + " #" +
                       std::to_string(target->second & 0xfffU) + "]" + match[3].str();
        }
        output << line;
        if (!input.eof()) output << '\n';
    }
    return output.str();
}

static std::map<std::string, uint64_t> registered_function_targets(
    const PluginBlob &blob, uint64_t code_va, uint64_t data_va) {
    std::map<std::string, uint64_t> targets;
    for (const auto &entry : blob.function_offsets)
        targets.emplace(entry.first, code_va + (uint64_t)entry.second);
    for (const auto &entry : blob.data_symbol_offsets)
        targets.emplace(entry.first, data_va + (uint64_t)entry.second);
    return targets;
}

static std::vector<uint8_t> patch_payload(
    const HookAction &action,
    const std::map<std::string, uint64_t> &function_targets = {}) {
    if (action.kind == "patch_asm") {
        auto source = normalize_registered_labels(
            action.data, action.address, function_targets);
        auto payload = assemble_aarch64(normalize_asm_text(source), action.address);
        if (action.size) {
            if (action.size % (int)payload.size() != 0)
                throw std::runtime_error("patch_asm size must be a multiple of assembled payload size");
            int repeat = action.size / (int)payload.size();
            std::vector<uint8_t> out;
            for (int i = 0; i < repeat; i++)
                out.insert(out.end(), payload.begin(), payload.end());
            return out;
        }
        return payload;
    }
    if (action.kind == "patch_hex")
        return parse_hex_payload(action.data);
    throw std::runtime_error("unsupported patch action");
}

static bool matches_expected(const std::filesystem::path &output_path,
                             const HookAction &action,
                             const std::vector<uint8_t> &payload,
                             const std::map<std::string, uint64_t> &function_targets = {}) {
    auto expected = action.kind == "patch_hex"
        ? parse_hex_payload(action.expected)
        : assemble_aarch64(normalize_asm_text(normalize_registered_labels(
              action.expected, action.address, function_targets)), action.address);
    if (expected.size() != payload.size())
        throw std::runtime_error("expected ASM must cover the same number of bytes as the patch");
    auto &binary = parse_binary(output_path);
    int off = file_offset(binary, action.address);
    auto data = read_file(output_path);
    if (off < 0 || expected.size() > data.size() - (size_t)off)
        throw std::runtime_error("patch out of range");
    if (memcmp(data.data() + off, expected.data(), expected.size()) != 0) {
        uint32_t current;
        uint32_t expected_code;
        memcpy(&current, data.data() + off, 4);
        memcpy(&expected_code, expected.data(), 4);
        char context[128];
        snprintf(context, sizeof(context), "current=0x%08x expected=0x%08x",
                 current, expected_code);
        diagnostic_warning("match", "expected instruction mismatch",
                           action.address, "asm_expected", context);
        return false;
    }
    return true;
}

static std::vector<uint8_t> get_original(BinaryImage &binary, uint64_t va, int size) {
    return binary.content_from_virtual_address(va, size);
}

static bool standard_pipeline(const std::filesystem::path &input_path,
                               const std::filesystem::path &output_path,
                               std::vector<PluginSpec> &plugins) {

    struct Compiled {
        PluginSpec *spec;
        PluginBlob blob;
        bool has_hooks = false;
        std::string requested_segment;
        std::string segment_name;
        int segment_size = 0;
        int code_offset = 0;
        uint64_t segment_va = 0;
        int segment_file_offset = 0;
        std::vector<uint8_t> content;
        std::string data_segment_name;
        int data_segment_size = 0;
        uint64_t data_segment_va = 0;
        int data_segment_file_offset = 0;
        std::vector<uint8_t> data_content;
    };
    std::vector<Compiled> compiled;
    for (auto &spec : plugins) {
        Compiled c;
        c.spec = &spec;
        c.blob = compile_plugin(spec.path, &input_path);
        spec.actions = c.blob.declarations;
        if (!c.blob.declarations.empty())
            compiled.push_back(std::move(c));
    }
    if (compiled.empty()) return false;

    auto &binary = parse_binary(std::filesystem::exists(output_path) ? output_path : input_path);

    const bool target_is_macho = binary.is_macho();

    struct HookSite {
        uint64_t va = 0;
        bool overrides_original = false;
        std::vector<std::pair<Compiled *, HookAction *>> handlers;
        std::vector<uint8_t> original;
        Compiled *owner = nullptr;
        int control_offset = 0;
        int hook_size = 4;
    };

    std::vector<std::pair<Compiled *, HookAction *>> direct;
    std::map<uint64_t, HookSite> replace_sites;
    std::map<uint64_t, HookSite> detour_sites;

    for (auto &cp : compiled) {
        for (auto &action : cp.blob.declarations) {
            uint64_t addr = action.address;
            if (addr == 0 && !action.objc_class.empty() && !action.selector.empty()) {
                auto value = armcave::find_objc_method(binary,
                                                       action.objc_class,
                                                       action.selector);
                if (value) addr = *value;
            }
            if (addr == 0 && !action.symbol.empty())
                addr = find_function_address(&binary, action.symbol);
            if (addr == 0 && !action.swift_name.empty()) {
                addr = find_function_address(&binary, action.swift_name);
            }
            if (addr == 0 && !action.signature.empty())
                addr = find_unique_signature(binary, action.signature);
            bool has_locator = !action.objc_class.empty() || !action.selector.empty() ||
                               !action.symbol.empty() || !action.swift_name.empty() ||
                               !action.signature.empty();
            if (addr == 0 && !has_locator && !action.handler.empty() &&
                action.kind != "new_cpp_func")
                addr = read_entry(binary);
            if (addr == 0 && action.kind != "new_asm_func" &&
                action.kind != "new_cpp_func")
                throw std::runtime_error(cp.spec->name + ": " + action.kind + " missing address");
            action.address = addr;

            if (action.kind == "patch_asm" || action.kind == "patch_hex") {
                if (references_data_label(action, cp.blob))
                    cp.has_hooks = true;
                direct.push_back({&cp, &action});
            } else if (action.kind == "new_asm_func") {
                cp.has_hooks = true;
            } else if (action.kind == "new_cpp_func") {
                cp.has_hooks = true;
            } else if (action.kind == "hook_replace") {
                cp.has_hooks = true;
                if (!action.segment.empty() && action.segment != "auto" && action.segment != "armcave") {
                    if (!cp.requested_segment.empty() && cp.requested_segment != action.segment)
                        throw std::runtime_error(cp.spec->name + ": one plugin cannot request multiple segments");
                    cp.requested_segment = action.segment;
                }
                auto &site = replace_sites[addr];
                site.va = addr;
                site.overrides_original = true;
                site.handlers.push_back({&cp, &action});
            } else if (action.kind == "hook_detour") {
                cp.has_hooks = true;
                if (!action.segment.empty() && action.segment != "auto" && action.segment != "armcave") {
                    if (!cp.requested_segment.empty() && cp.requested_segment != action.segment)
                        throw std::runtime_error(cp.spec->name + ": one plugin cannot request multiple segments");
                    cp.requested_segment = action.segment;
                }
                auto &site = detour_sites[addr];
                site.va = addr;
                site.handlers.push_back({&cp, &action});
            } else {
                throw std::runtime_error(cp.spec->name + ": unsupported action " + action.kind);
            }
        }
    }

    std::set<std::string> used_segments;
    std::vector<SegmentPlan> segment_plans;
    for (auto &cp : compiled) {
        if (!cp.has_hooks) continue;
        std::string prefix = cp.requested_segment.empty()
            ? cp.blob.default_segment : cp.requested_segment;
        cp.segment_name = make_plugin_seg_name(cp.spec->name, prefix, used_segments);
        if (cp.blob.has_writable_extra)
            cp.data_segment_name = make_plugin_data_seg_name(cp.segment_name, used_segments);
        for (auto &action : cp.blob.declarations)
            if (action.kind == "hook_replace" || action.kind == "hook_detour")
                action.segment = cp.segment_name;
    }

    for (const auto &cp : compiled) {
        if (!cp.has_hooks) continue;
        std::string conflict;
        if (has_plugin_segment(binary, cp.segment_name))
            conflict = seg_name(binary, cp.segment_name);
        else if (cp.blob.has_writable_extra &&
                 has_plugin_segment(binary, cp.data_segment_name))
            conflict = seg_name(binary, cp.data_segment_name);
        if (!conflict.empty())
            throw std::runtime_error(
                cp.spec->name + ": input already contains plugin segment " +
                conflict + "; use an unpatched original binary");
    }

    auto prepare_sites = [&](auto &sites) {
        auto &current = parse_binary(output_path);
        for (auto &[va, site] : sites) {
            site.original = get_original(current, va, kMaxHookWindow);
            if (site.original.empty())
                site.original = get_original(current, va, 12);
            if (site.original.empty())
                site.original = get_original(current, va, 4);
            if (site.original.size() < 4)
                throw std::runtime_error("cannot read hook window");
            if (site.handlers.empty())
                throw std::runtime_error("hook site has no handlers");
            site.owner = site.handlers.front().first;
        }
    };
    prepare_sites(detour_sites);
    prepare_sites(replace_sites);

    for (const auto &[va, site] : detour_sites)
        if (replace_sites.count(va))
            throw std::runtime_error("detour and replace overlap at 0x" +
                                     std::to_string(va));

    std::set<std::string> verified_segments;
    std::set<std::string> verified_data_segments;
    for (auto &cp : compiled) {
        if (!cp.has_hooks)
            continue;
        if (!verified_segments.insert(cp.segment_name).second)
            throw std::runtime_error("plugin code segment is not unique: " +
                                     cp.segment_name);
        for (auto &action : cp.blob.declarations)
        {
            if (action.kind != "hook_replace" && action.kind != "hook_detour")
                continue;
            cp.blob.for_action(action);
        }
    }

    std::map<Compiled *, std::vector<HookSite *>> owned_sites;
    for (auto &[va, site] : detour_sites) owned_sites[site.owner].push_back(&site);
    for (auto &[va, site] : replace_sites) owned_sites[site.owner].push_back(&site);
    std::map<Compiled *, std::map<HookAction *, int>> wrapper_offsets;

    for (auto &cp : compiled) {
        if (!cp.has_hooks) continue;
        int cursor = 0;
        for (auto *site : owned_sites[&cp]) {
            cursor = (cursor + 3) & ~3;
            site->control_offset = cursor;
            cursor += hook_dispatch_size((int)site->handlers.size(),
                                         kMaxHookWindow,
                                         site->overrides_original,
                                         target_is_macho);
        }
        for (auto &action : cp.blob.declarations) {
            bool is_hook = action.kind == "hook_replace" || action.kind == "hook_detour";
            bool is_registered_cpp = action.kind == "new_cpp_func" &&
                                     !action.register_args.empty();
            if ((!is_hook && !is_registered_cpp) ||
                (is_hook && action.register_args.empty()))
                continue;
            cursor = (cursor + 3) & ~3;
            wrapper_offsets[&cp][&action] = cursor;
            cursor += plugin_wrapper_max_size(action.register_args);
        }
        cursor = (cursor + 15) & ~15;
        cp.code_offset = cursor;
        int text_aligned = ((cp.blob.max_text_bytes()) + 15) & ~15;
        cursor += text_aligned;
        if (!cp.blob.has_writable_extra)
            cursor += (int)cp.blob.extra.size();
        cp.segment_size = std::max(cursor, 4);
        cp.content.resize(cp.segment_size, 0);
        if (cp.blob.has_writable_extra) {
            if (!verified_data_segments.insert(cp.data_segment_name).second)
                throw std::runtime_error("plugin data segment is not unique: " +
                                         cp.data_segment_name);
            cp.data_segment_size = (int)cp.blob.extra.size();
            cp.data_segment_size = (cp.data_segment_size + 15) & ~15;
            cp.data_content.resize(cp.data_segment_size, 0);
        }
    }

    for (auto &cp : compiled) {
        if (!cp.has_hooks) continue;
        SegmentPlan plan;
        plan.name = cp.segment_name;
        plan.size = cp.segment_size;
        plan.content.resize(cp.segment_size, 0);
        plan.writable = false;
        segment_plans.push_back(std::move(plan));
        if (cp.blob.has_writable_extra) {
            SegmentPlan dplan;
            dplan.name = cp.data_segment_name;
            dplan.size = cp.data_segment_size;
            dplan.content.resize(cp.data_segment_size, 0);
            dplan.writable = true;
            segment_plans.push_back(std::move(dplan));
        }
    }
    if (!segment_plans.empty())
        add_segments(output_path, segment_plans, output_path);

    auto &layout = parse_binary(output_path);
    for (auto &cp : compiled) {
        if (!cp.has_hooks) continue;
        cp.segment_va = seg_va(layout, cp.segment_name, cp.segment_size);
        cp.segment_file_offset = segment_file_offset(layout, cp.segment_name);
        if (cp.blob.has_writable_extra) {
            cp.data_segment_va = seg_va(layout, cp.data_segment_name, cp.data_segment_size);
            cp.data_segment_file_offset = segment_file_offset(layout, cp.data_segment_name);
        }
    }

    auto place = [](std::vector<uint8_t> &target, int offset,
                    const std::vector<uint8_t> &source) {
        if (offset < 0 || (size_t)offset + source.size() > target.size())
            throw std::runtime_error("plugin segment layout overflow");
        std::copy(source.begin(), source.end(), target.begin() + offset);
    };

    for (auto &cp : compiled) {
        if (!cp.has_hooks) continue;
        uint64_t code_va = cp.segment_va + cp.code_offset;
        int text_aligned = ((cp.blob.max_text_bytes()) + 15) & ~15;
        uint64_t data_va = !cp.blob.has_writable_extra
            ? (code_va + text_aligned)
            : cp.data_segment_va;
        auto built = cp.blob.build(code_va, data_va, &output_path);
        size_t code_bytes = std::min((size_t)text_aligned, built.size());
        place(cp.content, cp.code_offset,
              std::vector<uint8_t>(built.begin(), built.begin() + code_bytes));
        if (!cp.blob.extra.empty() && (int)built.size() > text_aligned) {
            auto data_start = built.begin() + text_aligned;
            auto data_end = built.end();
            std::vector<uint8_t> extra(data_start, data_end);
            if (cp.blob.has_writable_extra)
                std::copy(extra.begin(), extra.end(), cp.data_content.begin());
            else
                place(cp.content, cp.code_offset + text_aligned, extra);
        }

        for (auto &[action, offset] : wrapper_offsets[&cp]) {
            int entry = cp.blob.for_action(*action).entry_offset;
            auto wrapper = build_plugin_wrapper(action->register_args,
                cp.segment_va + offset, code_va + entry,
                action->kind == "new_cpp_func");
            place(cp.content, offset, wrapper);
        }
    }

    for (auto &[cp, action] : direct) {
        uint64_t code_va = cp->has_hooks
            ? cp->segment_va + (uint64_t)cp->code_offset : 0;
        int text_aligned = ((cp->blob.max_text_bytes()) + 15) & ~15;
        uint64_t data_va = cp->has_hooks
            ? (cp->blob.has_writable_extra ? cp->data_segment_va
                                           : code_va + (uint64_t)text_aligned)
            : 0;
        auto targets = registered_function_targets(cp->blob, code_va, data_va);
        for (auto &registered : cp->blob.declarations) {
            if (registered.kind != "new_cpp_func" || registered.register_args.empty())
                continue;
            auto wrapper = wrapper_offsets[cp].find(&registered);
            if (wrapper != wrapper_offsets[cp].end())
                targets[registered.function_name] = cp->segment_va +
                                                    (uint64_t)wrapper->second;
        }
        auto payload = patch_payload(*action, targets);
        if (action->has_expected && !matches_expected(output_path, *action, payload, targets))
            continue;
        printf("[%s] 0x%llx size=%zu\n", action->kind.c_str(),
               (unsigned long long)action->address, payload.size());
        patch_bytes_va(output_path, output_path, action->address, payload);
    }

    // Direct patches are applied after layout so registered function labels have
    // final addresses. Refresh hook windows because a direct patch may share a
    // site with a detour or replacement declaration.
    prepare_sites(detour_sites);
    prepare_sites(replace_sites);

    auto handler_va = [&](Compiled *cp, HookAction *action) {
        auto wrapper = wrapper_offsets[cp].find(action);
        if (wrapper != wrapper_offsets[cp].end())
            return cp->segment_va + (uint64_t)wrapper->second;
        return cp->segment_va + (uint64_t)cp->code_offset +
               (uint64_t)cp->blob.for_action(*action).entry_offset;
    };

    auto build_sites = [&](auto &sites) {
        for (auto &[va, site] : sites) {
            std::vector<uint64_t> handlers;
            for (auto &[cp, action] : site.handlers)
                handlers.push_back(handler_va(cp, action));
            uint64_t control_va = site.owner->segment_va + site.control_offset;
            site.hook_size = hook_window_size(va, control_va);
            if (site.hook_size > (int)site.original.size())
                throw std::runtime_error("hook window exceeds readable code at 0x" +
                                         std::to_string(va));
            site.original.resize(site.hook_size);
            auto control = build_hook_dispatch(control_va, va, site.hook_size, site.original,
                                               handlers, site.overrides_original,
                                               layout.is_macho());
            place(site.owner->content, site.control_offset, control);
        }
    };
    build_sites(detour_sites);
    build_sites(replace_sites);

    for (auto &cp : compiled) {
        if (!cp.has_hooks) continue;
        write_at_offset(output_path, cp.segment_file_offset, cp.content, cp.segment_size);
        if (cp.blob.has_writable_extra) {
            write_at_offset(output_path, cp.data_segment_file_offset,
                            cp.data_content, cp.data_segment_size);
            printf("[plugin] %s segment=%s size=%d (data segment=%s size=%d)\n",
                   cp.spec->name.c_str(), cp.segment_name.c_str(), cp.segment_size,
                   cp.data_segment_name.c_str(), cp.data_segment_size);
        } else {
            printf("[plugin] %s segment=%s size=%d\n", cp.spec->name.c_str(),
                   cp.segment_name.c_str(), cp.segment_size);
        }
    }

    auto patch_sites = [&](auto &sites) {
        for (auto &[va, site] : sites) {
            uint64_t control_va = site.owner->segment_va + site.control_offset;
            printf("[%s] 0x%llx segment=%s handlers=%zu\n",
                   site.overrides_original ? "hook_replace" : "hook_detour",
                   (unsigned long long)va, site.owner->segment_name.c_str(),
                   site.handlers.size());
            patch_hook_window(output_path, output_path, va, site.hook_size, control_va);
            printf("[done] 0x%llx\n", (unsigned long long)va);
        }
    };

    patch_sites(detour_sites);
    patch_sites(replace_sites);

    return true;
}

void run_pipeline(const std::filesystem::path &input_path,
                  const std::filesystem::path &output_path,
                  const std::filesystem::path &plugins_dir,
                  const std::vector<std::string> *plugin_names,
                  const std::string *whitelist,
                  const std::string *blacklist) {

    if (!std::filesystem::exists(input_path))
        throw std::runtime_error("input not found: " + input_path.string());

    std::vector<PluginSpec> plugins;

    if (std::filesystem::exists(plugins_dir)) {
        std::vector<std::string> names;
        if (plugin_names) {
            names = *plugin_names;
        } else {
            for (auto &entry : std::filesystem::directory_iterator(plugins_dir))
                if (entry.path().extension() == ".cpp")
                    names.push_back(entry.path().filename().string());
            std::sort(names.begin(), names.end());
        }

        std::set<std::string> filtered, excluded;
        if (whitelist) {
            size_t start = 0;
            while (true) {
                auto comma = whitelist->find(',', start);
                auto name = whitelist->substr(start, comma - start);
                while (!name.empty() && name[0] == ' ') name.erase(0, 1);
                while (!name.empty() && name.back() == ' ') name.pop_back();
                if (!name.empty()) filtered.insert(name);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
        if (blacklist) {
            size_t start = 0;
            while (true) {
                auto comma = blacklist->find(',', start);
                auto name = blacklist->substr(start, comma - start);
                while (!name.empty() && name[0] == ' ') name.erase(0, 1);
                while (!name.empty() && name.back() == ' ') name.pop_back();
                if (!name.empty()) excluded.insert(name);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }

        for (auto &name : names) {
            if (!filtered.empty() && !filtered.count(name)) continue;
            if (!excluded.empty() && excluded.count(name)) continue;
            auto path = plugins_dir / name;
            if (std::filesystem::exists(path))
                plugins.push_back(load_plugin(path));
        }
    }

    if (plugins.empty())
        throw std::runtime_error("no plugins found");

    if (!output_path.parent_path().empty())
        std::filesystem::create_directories(output_path.parent_path());
    std::filesystem::copy(input_path, output_path,
                          std::filesystem::copy_options::overwrite_existing);

    if (!standard_pipeline(input_path, output_path, plugins))
        throw std::runtime_error("no armcave actions found");

    if (is_macho(output_path)) {
#ifdef __APPLE__
        std::string cmd = "codesign --force --sign - " + shell_quote(output_path.string()) +
                          " >/dev/null 2>/dev/null";
        system(cmd.c_str());
#endif
    }
}

static std::string cpp_literal(const std::string &value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    out += '"';
    return out;
}

static std::string register_list(const std::vector<std::string> &registers) {
    std::string out;
    for (size_t i = 0; i < registers.size(); ++i) {
        if (i) out += ", ";
        out += registers[i];
    }
    return out;
}

void run_patch_script(const std::filesystem::path &script_path) {
    auto script = load_patch_script(script_path);
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto generated = std::filesystem::temp_directory_path() /
        ("armcave-script-" + std::to_string(stamp));
    std::filesystem::create_directories(generated);
    try {
        if (!script.plugins.empty() && std::filesystem::exists(script.plugins)) {
            for (const auto &entry : std::filesystem::directory_iterator(script.plugins)) {
                if (entry.path().extension() != ".cpp") continue;
                std::filesystem::copy_file(
                    entry.path(), generated / entry.path().filename(),
                    std::filesystem::copy_options::overwrite_existing);
            }
        }
        for (size_t index = 0; index < script.hooks.size(); ++index) {
            const auto &hook = script.hooks[index];
            std::filesystem::path source = hook.source;
            if (source.is_relative()) source = script.path.parent_path() / source;
            if (!std::filesystem::exists(source))
                throw std::runtime_error("patch source not found: " + source.string());
            auto generated_source = generated /
                ("patch_script_" + std::to_string(index) + ".cpp");
            std::ofstream output(generated_source);
            if (!output)
                throw std::runtime_error("cannot create generated patch plugin");
            output << "#include \"armcave.h\"\n";
            output << "#include " << cpp_literal(std::filesystem::absolute(source).string()) << "\n";
            output << "extern \"C\" void init(void) {\n";
            std::string registers = register_list(hook.register_args);
            auto suffix = registers.empty() ? std::string() : ", " + registers;
            if (!hook.objc_class.empty() && !hook.selector.empty()) {
                output << "    " << (hook.kind == "hook_detour"
                    ? "hook_detour_objc_method" : "hook_objc_method") << "("
                    << cpp_literal(hook.objc_class) << ", "
                    << cpp_literal(hook.selector) << ", "
                    << hook.handler << suffix << ");\n";
            } else if (!hook.signature.empty()) {
                output << "    " << (hook.kind == "hook_detour"
                    ? "hook_detour_signature" : "hook_replace_signature") << "("
                    << cpp_literal(hook.signature) << ", "
                    << hook.handler << suffix << ");\n";
            } else {
                output << "    " << (hook.kind == "hook_detour"
                    ? "hook_detour_symbol" : "hook_replace_symbol") << "("
                    << cpp_literal(hook.function) << ", "
                    << hook.handler << suffix << ");\n";
            }
            output << "}\n";
        }
        run_pipeline(script.binary, script.output, generated);
        std::error_code ec;
        std::filesystem::remove_all(generated, ec);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(generated, ec);
        throw;
    }
}
