#include "symbols.h"
#include "compiler.h"
#include <cstring>
#include <set>
#include <cstdio>

static std::vector<std::string> names(const std::string &symbol_name) {
    std::vector<std::string> out;
    out.push_back(symbol_name);
    std::string s = symbol_name;
    if (!s.empty() && s[0] == '_')
        out.push_back(s.substr(1));
    else
        out.push_back("_" + s);
    out.push_back("__" + [&]() {
        auto t = symbol_name;
        while (!t.empty() && t[0] == '_') t.erase(0, 1);
        return t;
    }());
    return out;
}

static int resolve_marker(const std::string &symbol_name, const std::string &marker) {
    for (auto &name : names(symbol_name)) {
        auto idx = name.find(marker);
        if (idx == std::string::npos) continue;
        std::string raw = name.substr(idx + marker.size());
        while (!raw.empty() && (raw.back() == 'U' || raw.back() == 'L' || raw.back() == 'u' || raw.back() == 'l'))
            raw.pop_back();
        return (int)strtoull(raw.c_str(), nullptr, 0);
    }
    return 0;
}

static int resolve_armcave_va(const std::string &symbol_name) {
    return resolve_marker(symbol_name, "armcave_va_");
}

static int resolve_armcave_data(const std::string &symbol_name) {
    return resolve_marker(symbol_name, "armcave_data_");
}

static int resolve_armcave_tco(const std::string &symbol_name, uint64_t base) {
    int val = resolve_marker(symbol_name, "armcave_tco_");
    return val ? (int)(val + base) : 0;
}

static int resolve_via_symbol_table(BinaryImage *binary, const std::string &symbol_name) {
    if (binary->is_elf()) {
        for (const auto &candidate : names(symbol_name)) {
            auto direct = binary->symbol_address(candidate);
            if (direct) return (int)*direct;
            auto stub = binary->import_stub(candidate);
            if (stub) return (int)*stub;
        }
        return 0;
    }
    BinarySection *stubs = nullptr;
    for (auto &s : binary->sections())
        if (s.name == "__stubs") { stubs = &s; break; }
    if (!stubs) return 0;
    auto ns = names(symbol_name);
    std::set<std::string> name_set(ns.begin(), ns.end());
    int size = stubs->reserved2 ? (int)stubs->reserved2 : 12;
    for (int i = 0; i < (int)stubs->size / size; i++) {
        int idx = (int)stubs->reserved1 + i;
        auto *symbol = binary->indirect_symbol(idx);
        if (symbol && name_set.count(*symbol))
            return (int)(stubs->virtual_address + i * size);
    }
    return 0;
}

static int resolve_import_slot(BinaryImage *binary, const std::string &symbol_name) {
    if (binary->is_elf()) {
        for (const auto &candidate : names(symbol_name)) {
            auto slot = binary->import_slot(candidate);
            if (slot) return (int)*slot;
        }
        return 0;
    }
    auto ns = names(symbol_name);
    std::set<std::string> name_set(ns.begin(), ns.end());
    for (auto &sec : binary->sections()) {
        auto sn = sec.name;
        if (sn != "__got" && sn != "__la_symbol_ptr" && sn != "__nl_symbol_ptr")
            continue;
        for (int i = 0; i < (int)sec.size / 8; i++) {
            int idx = (int)sec.reserved1 + i;
            auto *symbol = binary->indirect_symbol(idx);
            if (symbol && name_set.count(*symbol))
                return (int)(sec.virtual_address + i * 8);
        }
    }
    return 0;
}

std::vector<std::pair<std::string, std::string>> list_available_symbols(
    const std::filesystem::path &binary_path) {
    auto binary = BinaryImage::parse(binary_path);
    if (!binary)
        throw std::runtime_error("unsupported binary file");
    std::vector<std::pair<std::string, std::string>> out;
    if (binary->is_elf()) {
        for (const auto &symbol : binary->symbols()) {
            if (symbol.name.empty() || symbol.undefined() || !symbol.value) continue;
            char addr[32];
            snprintf(addr, sizeof(addr), "0x%llx", (unsigned long long)symbol.value);
            out.emplace_back(symbol.name, addr);
        }
        for (const auto &item : binary->imports()) {
            if (item.name.empty() || !item.stub_address) continue;
            char addr[32];
            snprintf(addr, sizeof(addr), "0x%llx", (unsigned long long)item.stub_address);
            out.emplace_back(item.name, addr);
        }
        return out;
    }
    BinarySection *stubs = binary->section("__stubs");
    if (!stubs) return out;
    int size = stubs->reserved2 ? (int)stubs->reserved2 : 12;
    for (int i = 0; i < (int)stubs->size / size; i++) {
        int idx = (int)stubs->reserved1 + i;
        auto *symbol = binary->indirect_symbol(idx);
        if (symbol && !symbol->empty()) {
            char addr[32];
            snprintf(addr, sizeof(addr), "0x%llx", (unsigned long long)(stubs->virtual_address + i * size));
            out.emplace_back(*symbol, addr);
        }
    }
    return out;
}

static void patch_branch26(std::vector<uint8_t> &data, int off, int src, int dst) {
    int imm = (dst - src) >> 2;
    if (imm < -(1 << 25) || imm >= (1 << 25))
        throw std::runtime_error("branch relocation out of range");
    uint32_t insn;
    memcpy(&insn, data.data() + off, 4);
    insn = (insn & 0xFC000000) | (imm & 0x03FFFFFF);
    memcpy(data.data() + off, &insn, 4);
}

static void patch_page21(std::vector<uint8_t> &data, int off, int pc, int target) {
    int diff = ((target & ~0xFFF) - (pc & ~0xFFF)) >> 12;
    uint32_t imm = diff & 0x1FFFFF;
    uint32_t insn;
    memcpy(&insn, data.data() + off, 4);
    insn = (insn & 0x9F00001F) | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5);
    memcpy(data.data() + off, &insn, 4);
}

static void patch_pageoff12(std::vector<uint8_t> &data, int off, int target) {
    uint32_t insn;
    memcpy(&insn, data.data() + off, 4);
    insn = (insn & 0xFFC003FF) | ((target & 0xFFF) << 10);
    memcpy(data.data() + off, &insn, 4);
}

static void patch_got_load_pageoff12(std::vector<uint8_t> &data, int off, int target) {
    uint32_t insn;
    memcpy(&insn, data.data() + off, 4);
    int scale = ((insn & 0xC0000000) == 0xC0000000) ? 8 : 4;
    int imm = (target & 0xFFF) / scale;
    insn = (insn & 0xFFC003FF) | (imm << 10);
    memcpy(data.data() + off, &insn, 4);
}

static void patch_ldr_to_add(std::vector<uint8_t> &data, int off, int target) {
    uint32_t insn;
    memcpy(&insn, data.data() + off, 4);
    uint32_t opcode;
    if ((insn & 0xFF000000) == 0xF9000000)
        opcode = 0x91000000;
    else if ((insn & 0xFF000000) == 0xB9000000)
        opcode = 0x11000000;
    else {
        char message[96];
        snprintf(message, sizeof(message),
                 "unexpected GOT-load instruction 0x%08x at text offset 0x%x", insn, off);
        throw std::runtime_error(message);
    }
    uint32_t imm12 = target & 0xFFF;
    insn = opcode | (imm12 << 10) | (insn & 0x000003FF);
    memcpy(data.data() + off, &insn, 4);
}

std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
resolve_plugin_relocs(
    const std::vector<uint8_t> &text,
    const std::vector<uint8_t> &extra,
    const std::vector<RelocEntry> &relocs,
    const std::map<std::string, int> &offsets,
    const std::filesystem::path &binary_path,
    uint64_t text_va,
    uint64_t data_va,
    uint64_t armcave_base) {

    auto binary = BinaryImage::parse(binary_path);
    if (!binary)
        throw std::runtime_error("failed to parse " + binary_path.string());

    std::vector<uint8_t> text_buf = text;
    std::vector<uint8_t> extra_buf = extra;

    for (auto &r : relocs) {
        int t = r.type;
        int off = r.address;
        auto &name = r.symbol_name;
        uint64_t val = r.symbol_value;
        auto &section = r.symbol_section;

        if (t == 2 && !name.empty()) {
            int dst = 0;
            if (val == 0 && section.empty()) {
                dst = resolve_armcave_va(name);
                if (!dst) dst = resolve_armcave_tco(name, armcave_base);
                if (!dst) dst = resolve_via_symbol_table(binary.get(), name);
            } else {
                dst = (int)(text_va + val);
            }
            if (!dst)
                throw std::runtime_error("unresolved symbol: " + name);
            patch_branch26(text_buf, off, (int)(text_va + off), dst);

        } else if (t == 3) {
            int dst = 0;
            if (!name.empty()) dst = resolve_armcave_data(name);
            if (dst) {
                patch_page21(text_buf, off, (int)(text_va + off), dst);
            } else if (!section.empty()) {
                int target;
                if (section == "__text")
                    target = (int)(text_va + val);
                else {
                    auto it = offsets.find(section);
                    if (it == offsets.end())
                        throw std::runtime_error("unknown section: " + section);
                    target = (int)(data_va + it->second + val);
                }
                patch_page21(text_buf, off, (int)(text_va + off), target);
            }

        } else if (t == 4) {
            int dst = 0;
            if (!name.empty()) dst = resolve_armcave_data(name);
            if (dst) {
                patch_pageoff12(text_buf, off, dst);
            } else if (!section.empty()) {
                int target;
                if (section == "__text")
                    target = (int)(text_va + val);
                else {
                    auto it = offsets.find(section);
                    if (it == offsets.end())
                        throw std::runtime_error("unknown section: " + section);
                    target = (int)(data_va + it->second + val);
                }
                patch_pageoff12(text_buf, off, target);
            }

        } else if (t == 5 && !name.empty()) {
            int dst = resolve_armcave_data(name);
            if (dst) {
                patch_page21(text_buf, off, (int)(text_va + off), dst);
            } else {
                int target = resolve_import_slot(binary.get(), name);
                if (!target)
                    throw std::runtime_error("unresolved import slot: " + name);
                patch_page21(text_buf, off, (int)(text_va + off), target);
            }

        } else if (t == 6 && !name.empty()) {
            int dst = resolve_armcave_data(name);
            if (dst) {
                patch_ldr_to_add(text_buf, off, dst);
            } else {
                int target = resolve_import_slot(binary.get(), name);
                if (!target)
                    throw std::runtime_error("unresolved import slot: " + name);
                patch_got_load_pageoff12(text_buf, off, target);
            }
        }
    }

    return {text_buf, extra_buf};
}
