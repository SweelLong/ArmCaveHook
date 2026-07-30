#include "pipeline.h"
#include "compiler.h"
#include "patcher.h"
#include "plugin.h"
#include "segment.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <regex>
#include <map>
#include <set>
#include <memory>
#include <array>
#include <cstdint>

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

static std::vector<uint8_t> patch_payload(const HookAction &action) {
    if (action.kind == "patch_asm") {
        std::string asm_text = action.data;
        std::replace(asm_text.begin(), asm_text.end(), ';', '\n');
        auto payload = assemble_aarch64(asm_text);
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
    throw std::runtime_error("unsupported patch action");
}

static bool matches_expected(const std::filesystem::path &output_path,
                             const HookAction &action,
                             const std::vector<uint8_t> &payload) {
    auto expected = assemble_aarch64(action.expected);
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
        printf("[asm:skip] 0x%llx current=0x%08x expected=0x%08x (%s)\n",
               (unsigned long long)action.address, current, expected_code,
               action.expected.c_str());
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
        // Data segment (RW-) for plugin's static/global data
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
    };

    std::vector<HookAction *> direct;
    std::map<uint64_t, HookSite> replace_sites;
    std::map<uint64_t, HookSite> detour_sites;

    for (auto &cp : compiled) {
        for (auto &action : cp.blob.declarations) {
            uint64_t addr = action.address;
            if (addr == 0 && !action.handler.empty())
                addr = read_entry(binary);
            if (addr == 0)
                throw std::runtime_error(cp.spec->name + ": " + action.kind + " missing address");
            action.address = addr;

            if (action.kind == "patch_asm") {
                direct.push_back(&action);
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

    for (auto *action : direct) {
        auto payload = patch_payload(*action);
        if (action->has_expected && !matches_expected(output_path, *action, payload)) {
            continue;
        }
        printf("[%s] 0x%llx size=%zu\n", action->kind.c_str(),
               (unsigned long long)action->address, payload.size());
        patch_bytes_va(output_path, output_path, action->address, payload);
    }

    auto prepare_sites = [&](auto &sites) {
        auto &current = parse_binary(output_path);
        for (auto &[va, site] : sites) {
            site.original = get_original(current, va, 4);
            if (site.original.size() != 4)
                throw std::runtime_error("cannot read hook window");
            if (site.handlers.empty())
                throw std::runtime_error("hook site has no handlers");
            site.owner = site.handlers.front().first;
        }
    };
    prepare_sites(detour_sites);
    prepare_sites(replace_sites);

    std::set<std::string> used_segments;
    std::vector<SegmentPlan> segment_plans;
    for (auto &cp : compiled) {
        if (!cp.has_hooks) continue;
        std::string prefix = cp.requested_segment.empty()
            ? cp.blob.default_segment : cp.requested_segment;
        cp.segment_name = make_plugin_seg_name(cp.spec->name, prefix, used_segments);
        for (auto &action : cp.blob.declarations)
            if (action.kind == "hook_replace" || action.kind == "hook_detour")
                action.segment = cp.segment_name;
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
            cursor += hook_dispatch_size((int)site->handlers.size(), 4,
                                         site->overrides_original,
                                         target_is_macho);
        }
        for (auto &action : cp.blob.declarations) {
            if ((action.kind != "hook_replace" && action.kind != "hook_detour") ||
                action.register_args.empty())
                continue;
            cursor = (cursor + 3) & ~3;
            wrapper_offsets[&cp][&action] = cursor;
            cursor += plugin_wrapper_size(action.register_args);
        }
        cursor = (cursor + 15) & ~15;
        cp.code_offset = cursor;
        // Code segment only contains text (not extra data)
        int text_aligned = ((int)cp.blob.text.size() + 15) & ~15;
        cursor += text_aligned;
        cp.segment_size = std::max(cursor, 4);
        cp.content.resize(cp.segment_size, 0);
        // Data segment contains extra (static/global data) if present
        if (!cp.blob.extra.empty()) {
            cp.data_segment_name = cp.segment_name + "_data";
            cp.data_segment_size = (int)cp.blob.extra.size();
            // Align to 16 for safe ADRP/LDR access
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
        plan.writable = false;  // R-X
        segment_plans.push_back(std::move(plan));
        // Create separate RW- data segment if plugin has static data
        if (!cp.blob.extra.empty()) {
            SegmentPlan dplan;
            dplan.name = cp.data_segment_name;
            dplan.size = cp.data_segment_size;
            dplan.content.resize(cp.data_segment_size, 0);
            dplan.writable = true;  // RW-
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
        if (!cp.blob.extra.empty()) {
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
        // data_va points to the separate RW- data segment (or fallback to
        // code_va + aligned_text if no extra, which shouldn't happen for
        // plugins with static data but keeps the old behavior safe)
        uint64_t data_va = cp.blob.extra.empty()
            ? (code_va + (((int)cp.blob.text.size() + 15) & ~15))
            : cp.data_segment_va;
        auto built = cp.blob.build(code_va, data_va, &output_path);
        // build() returns text + padding + extra concatenated; split them
        int text_aligned = ((int)cp.blob.text.size() + 15) & ~15;
        // Place text (code) into the R-X segment
        place(cp.content, cp.code_offset,
              std::vector<uint8_t>(built.begin(),
                                   built.begin() + std::min((int)cp.blob.text.size(), (int)built.size())));
        // Place extra (data) into the RW- segment if present
        if (!cp.blob.extra.empty() && (int)built.size() > text_aligned) {
            auto data_start = built.begin() + text_aligned;
            auto data_end = built.end();
            std::copy(data_start, data_end, cp.data_content.begin());
        }

        for (auto &[action, offset] : wrapper_offsets[&cp]) {
            int entry = cp.blob.for_action(*action).entry_offset;
            auto wrapper = build_plugin_wrapper(action->register_args,
                cp.segment_va + offset, code_va + entry);
            place(cp.content, offset, wrapper);
        }
    }

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
            auto control = build_hook_dispatch(control_va, va, 4, site.original,
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
        if (!cp.blob.extra.empty()) {
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
            patch_hook_window(output_path, output_path, va, 4, control_va);
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
