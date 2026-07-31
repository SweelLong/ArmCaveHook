#include "aarch64/function_ir.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace armcave {
namespace aarch64 {

namespace {

const BinarySection *code_section(const BinaryImage &binary, uint64_t address) {
    for (const auto &section : binary.sections()) {
        if ((section.name == "__text" || section.name == ".text") &&
            section.virtual_address <= address &&
            address - section.virtual_address < section.size)
            return &section;
    }
    for (const auto &section : binary.sections()) {
        if (section.virtual_address <= address &&
            address - section.virtual_address < section.size) {
            for (const auto &segment : binary.segments())
                if (segment.virtual_address <= address &&
                    address - segment.virtual_address < segment.file_size &&
                    (segment.init_protection & 4))
                    return &section;
        }
    }
    return nullptr;
}

bool printable_string(const BinaryImage &binary, uint64_t address) {
    auto offset = binary.virtual_address_to_offset(address);
    if (!offset || *offset >= binary.data().size()) return false;
    size_t length = 0;
    while (*offset + length < binary.data().size() && length < 256) {
        unsigned char c = binary.data()[(size_t)*offset + length];
        if (c == 0) return length >= 4;
        if (c < 0x20 || c > 0x7e) return false;
        ++length;
    }
    return false;
}

void unique_sort(std::vector<uint64_t> &values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

}

FunctionIR analyze_function(const BinaryImage &binary,
                            uint64_t entry_address,
                            std::size_t max_instructions) {
    auto *section = code_section(binary, entry_address);
    if (!section) return {};
    auto code = section->content(binary.data());
    if (code.empty()) return {};
    auto function = analyze_function(code, section->virtual_address,
                                     entry_address, max_instructions);
    for (auto constant : function.constants)
        if (printable_string(binary, constant))
            function.strings.push_back(constant);
    unique_sort(function.strings);
    function.fingerprint = function_fingerprint(function);
    return function;
}

std::vector<FunctionIR> discover_functions(const BinaryImage &binary,
                                           std::size_t max_functions,
                                           std::size_t max_instructions) {
    std::set<uint64_t> entries;
    if (binary.entrypoint()) entries.insert(binary.entrypoint());
    for (const auto &symbol : binary.symbols()) {
        if (symbol.undefined() || !symbol.value) continue;
        if (code_section(binary, symbol.value)) entries.insert(symbol.value);
    }
    std::vector<uint64_t> pending(entries.begin(), entries.end());
    std::set<uint64_t> analyzed;
    std::vector<FunctionIR> result;
    while (!pending.empty() && result.size() < max_functions) {
        uint64_t entry = pending.back();
        pending.pop_back();
        if (!analyzed.insert(entry).second) continue;
        auto function = analyze_function(binary, entry, max_instructions);
        if (function.blocks.empty()) continue;
        for (auto call : function.calls)
            if (code_section(binary, call) && !analyzed.count(call)) {
                entries.insert(call);
                pending.push_back(call);
            }
        result.push_back(std::move(function));
    }
    return result;
}

}
}
