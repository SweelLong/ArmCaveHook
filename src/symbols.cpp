#include "symbols.h"
#include "compiler.h"
#include "aarch64/encoder.h"
#include <cstring>
#include <set>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#ifdef __GNUG__
#include <cxxabi.h>
#endif

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

static uint64_t resolve_marker(const std::string &symbol_name, const std::string &marker) {
    for (auto &name : names(symbol_name)) {
        auto idx = name.find(marker);
        if (idx == std::string::npos) continue;
        std::string raw = name.substr(idx + marker.size());
        while (!raw.empty() && (raw.back() == 'U' || raw.back() == 'L' || raw.back() == 'u' || raw.back() == 'l'))
            raw.pop_back();
        return strtoull(raw.c_str(), nullptr, 0);
    }
    return 0;
}

static uint64_t resolve_armcave_va(const std::string &symbol_name) {
    return resolve_marker(symbol_name, "armcave_va_");
}

static uint64_t resolve_armcave_data(const std::string &symbol_name) {
    return resolve_marker(symbol_name, "armcave_data_");
}

static uint64_t resolve_via_symbol_table(BinaryImage *binary, const std::string &symbol_name) {
    if (binary->is_elf()) {
        for (const auto &candidate : names(symbol_name)) {
            auto direct = binary->symbol_address(candidate);
            if (direct) return *direct;
            auto stub = binary->import_stub(candidate);
            if (stub) return *stub;
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
            return stubs->virtual_address + (uint64_t)i * (uint64_t)size;
    }
    return 0;
}

static std::string demangled_name(const std::string &name) {
#ifdef __GNUG__
    std::vector<std::string> candidates{name};
    if (!name.empty() && name[0] == '_') candidates.push_back(name.substr(1));
    for (const auto &candidate : candidates) {
        int status = 0;
        char *value = abi::__cxa_demangle(candidate.c_str(), nullptr, nullptr, &status);
        if (status == 0 && value) {
            std::string result(value);
            free(value);
            return result;
        }
        free(value);
    }
#endif
    return {};
}

uint64_t find_function_address(BinaryImage *binary, const std::string &query) {
    if (!binary || query.empty()) return 0;
    std::set<uint64_t> matches;
    for (const auto &symbol : binary->symbols()) {
        if (symbol.undefined() || !symbol.value) continue;
        std::string plain = symbol.name;
        if (!plain.empty() && plain[0] == '_') plain.erase(plain.begin());
        std::string demangled = demangled_name(symbol.name);
        bool match = symbol.name == query || plain == query ||
                     symbol.name.find(query) != std::string::npos ||
                     plain.find(query) != std::string::npos ||
                     (!demangled.empty() && (demangled == query ||
                                              demangled.find(query) != std::string::npos));
        if (match) matches.insert(symbol.value);
    }
    if (matches.size() == 1) return *matches.begin();
    for (const auto &candidate : names(query)) {
        auto value = binary->symbol_address(candidate);
        if (value) matches.insert(*value);
    }
    return matches.size() == 1 ? *matches.begin() : 0;
}

static uint64_t resolve_import_slot(BinaryImage *binary, const std::string &symbol_name) {
    if (binary->is_elf()) {
        for (const auto &candidate : names(symbol_name)) {
            auto slot = binary->import_slot(candidate);
            if (slot) return *slot;
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
                return sec.virtual_address + (uint64_t)i * 8;
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

static uint32_t read_word(const std::vector<uint8_t> &data, int off) {
    if (off < 0 || (size_t)off + 4 > data.size())
        throw std::runtime_error("relocation offset is outside text");
    uint32_t insn;
    memcpy(&insn, data.data() + off, 4);
    return insn;
}

static void write_word(std::vector<uint8_t> &data, int off, uint32_t insn) {
    if (off < 0 || (size_t)off + 4 > data.size())
        throw std::runtime_error("relocation offset is outside text");
    memcpy(data.data() + off, &insn, 4);
}

static void write_qword(std::vector<uint8_t> &data, int off, uint64_t value) {
    if (off < 0 || (size_t)off + 8 > data.size())
        throw std::runtime_error("relocation offset is outside text");
    memcpy(data.data() + off, &value, 8);
}

static void patch_branch26(std::vector<uint8_t> &data, int off,
                           uint64_t src, uint64_t dst) {
    uint32_t insn = read_word(data, off);
    if (!armcave::aarch64::fits_branch26(src, dst))
        throw std::runtime_error("branch relocation out of range");
    int64_t delta = dst >= src ? (int64_t)(dst - src) : -(int64_t)(src - dst);
    uint32_t encoded = (insn & 0xfc000000U) |
        ((uint32_t)(delta / 4) & 0x03ffffffU);
    write_word(data, off, encoded);
}

static void patch_page21(std::vector<uint8_t> &data, int off,
                         uint64_t pc, uint64_t target) {
    uint32_t insn = read_word(data, off);
    uint32_t encoded = armcave::aarch64::encode_adrp(
        (uint8_t)(insn & 0x1fU), pc, target);
    write_word(data, off, encoded);
}

static void patch_pageoff12(std::vector<uint8_t> &data, int off, uint64_t target) {
    uint32_t insn = read_word(data, off);
    int scale = 1;
    if ((insn & 0x3B000000) == 0x39000000)
        scale = 1 << ((insn >> 30) & 3);
    if ((target & 0xfffU) % (uint64_t)scale)
        throw std::runtime_error("pageoff relocation is not aligned");
    uint64_t imm = (target & 0xfffU) / (uint64_t)scale;
    if (imm > 0xfffU)
        throw std::runtime_error("pageoff relocation is out of range");
    insn = (insn & 0xFFC003FF) | (imm << 10);
    write_word(data, off, insn);
}

static void patch_got_load_pageoff12(std::vector<uint8_t> &data, int off,
                                     uint64_t target) {
    uint32_t insn = read_word(data, off);
    int scale = ((insn & 0xC0000000) == 0xC0000000) ? 8 : 4;
    if ((target & 0xfffU) % (uint64_t)scale)
        throw std::runtime_error("GOT pageoff relocation is not aligned");
    uint64_t imm = (target & 0xfffU) / (uint64_t)scale;
    if (imm > 0xfffU)
        throw std::runtime_error("GOT pageoff relocation is out of range");
    insn = (insn & 0xFFC003FF) | (imm << 10);
    write_word(data, off, insn);
}

static void patch_ldr_to_add(std::vector<uint8_t> &data, int off, uint64_t target) {
    uint32_t insn = read_word(data, off);
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
    write_word(data, off, insn);
}

static uint64_t relocation_target(const RelocEntry &reloc,
                                  BinaryImage *binary,
                                  const std::map<std::string, int> &offsets,
                                  uint64_t text_va, uint64_t data_va) {
    if (reloc.has_absolute_target)
        return reloc.absolute_target + reloc.addend;
    if (reloc.symbol_value == 0 && reloc.symbol_section.empty()) {
        uint64_t target = resolve_armcave_va(reloc.symbol_name);
        if (!target) target = resolve_via_symbol_table(binary, reloc.symbol_name);
        if (!target) throw std::runtime_error("unresolved symbol: " + reloc.symbol_name);
        return target + reloc.addend;
    }
    if (reloc.symbol_section == "__text")
        return text_va + reloc.symbol_value + reloc.addend;
    auto it = offsets.find(reloc.symbol_section);
    if (it == offsets.end())
        throw std::runtime_error("unknown section: " + reloc.symbol_section);
    return data_va + (uint64_t)it->second + reloc.symbol_value + reloc.addend;
}

static uint64_t data_relocation_target(const RelocEntry &reloc,
                                       BinaryImage *binary,
                                       const std::map<std::string, int> &offsets,
                                       uint64_t text_va, uint64_t data_va) {
    uint64_t target = resolve_armcave_data(reloc.symbol_name);
    if (target) return target + reloc.addend;
    if (!reloc.symbol_section.empty())
        return relocation_target(reloc, binary, offsets,
                                 text_va, data_va);
    target = resolve_import_slot(binary, reloc.symbol_name);
    if (!target) throw std::runtime_error("unresolved import slot: " + reloc.symbol_name);
    return target + reloc.addend;
}

std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
resolve_plugin_relocs(
    const std::vector<uint8_t> &text,
    const std::vector<uint8_t> &extra,
    const std::vector<RelocEntry> &relocs,
    const std::map<std::string, int> &offsets,
    const std::filesystem::path &binary_path,
    uint64_t text_va,
    uint64_t data_va) {

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
        int64_t addend = r.addend;

        if (t == 0 && !name.empty()) {
            write_qword(text_buf, off,
                        relocation_target(r, binary.get(), offsets,
                                          text_va, data_va));

        } else if (t == 2) {
            uint64_t dst = relocation_target(r, binary.get(), offsets,
                                              text_va, data_va);
            uint64_t src = text_va + (uint64_t)off;
            uint32_t instruction = read_word(text_buf, off);
            if (!armcave::aarch64::fits_branch26(src, dst)) {
                uint64_t veneer = text_va + text_buf.size();
                auto sequence = armcave::aarch64::make_address_sequence(
                    veneer, dst, 16);
                text_buf.insert(text_buf.end(), sequence.begin(), sequence.end());
                uint32_t jump = armcave::aarch64::encode_br(16);
                text_buf.push_back((uint8_t)jump);
                text_buf.push_back((uint8_t)(jump >> 8));
                text_buf.push_back((uint8_t)(jump >> 16));
                text_buf.push_back((uint8_t)(jump >> 24));
                if (!armcave::aarch64::fits_branch26(src, veneer))
                    throw std::runtime_error("branch veneer is out of range: " + name);
                dst = veneer;
            }
            patch_branch26(text_buf, off, src, dst);

        } else if (t == 3) {
            patch_page21(text_buf, off, text_va + (uint64_t)off,
                         data_relocation_target(r, binary.get(), offsets,
                                                text_va, data_va));

        } else if (t == 4) {
            patch_pageoff12(text_buf, off,
                            data_relocation_target(r, binary.get(), offsets,
                                                   text_va, data_va));

        } else if (t == 5 && !name.empty()) {
            patch_page21(text_buf, off, text_va + (uint64_t)off,
                         data_relocation_target(r, binary.get(), offsets,
                                                text_va, data_va));

        } else if (t == 6 && !name.empty()) {
            uint64_t target = data_relocation_target(r, binary.get(), offsets,
                                                     text_va, data_va);
            if (resolve_armcave_data(name))
                patch_ldr_to_add(text_buf, off, target);
            else
                patch_got_load_pageoff12(text_buf, off, target);
        }
    }

    return {text_buf, extra_buf};
}
