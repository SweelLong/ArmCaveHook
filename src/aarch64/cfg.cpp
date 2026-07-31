#include "aarch64/cfg.h"

#include "aarch64/decoder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace armcave {
namespace aarch64 {

namespace {

uint32_t read_word(const std::vector<uint8_t> &code, std::size_t offset) {
    uint32_t word = 0;
    std::memcpy(&word, code.data() + offset, 4);
    return word;
}

bool is_return(uint32_t instruction) {
    return (instruction & 0xfffffc1fU) == 0xd65f0000U;
}

bool in_range(uint64_t address, uint64_t base, std::size_t size) {
    return address >= base && address - base < size;
}

}

ControlFlowGraph analyze_cfg(const std::vector<uint8_t> &code,
                             uint64_t base_address,
                             uint64_t entry_address,
                             std::size_t max_instructions) {
    ControlFlowGraph graph;
    if ((base_address & 3U) || (entry_address & 3U) || code.size() % 4 ||
        !in_range(entry_address, base_address, code.size()))
        return graph;

    std::vector<uint64_t> worklist{entry_address};
    std::set<uint64_t> queued{entry_address};
    std::set<uint64_t> built;
    std::size_t instruction_count = 0;
    while (!worklist.empty() && instruction_count < max_instructions) {
        uint64_t block_address = worklist.back();
        worklist.pop_back();
        if (built.count(block_address)) continue;
        std::size_t offset = (std::size_t)(block_address - base_address);
        BasicBlock block;
        block.address = block_address;
        while (offset + 4 <= code.size() && instruction_count < max_instructions) {
            uint64_t address = base_address + offset;
            if (address != block_address && queued.count(address)) break;
            uint32_t instruction = read_word(code, offset);
            auto decoded = decode(instruction, address);
            ++instruction_count;
            offset += 4;
            block.size += 4;
            bool stop = is_return(instruction);
            if (decoded.has_target && is_branch(decoded.kind)) {
                block.successors.push_back(decoded.target);
                if (in_range(decoded.target, base_address, code.size()) &&
                    !built.count(decoded.target) && queued.insert(decoded.target).second)
                    worklist.push_back(decoded.target);
                if (decoded.kind != InstructionKind::BL)
                    stop = true;
            }
            if (stop) break;
            uint64_t next = base_address + offset;
            if (queued.count(next) && next != block_address) break;
        }
        if (block.size == 0) continue;
        uint64_t fallthrough = block.address + block.size;
        uint32_t last = read_word(code, (std::size_t)(fallthrough - base_address - 4));
        auto last_decoded = decode(last, fallthrough - 4);
        if (!is_return(last) && last_decoded.kind != InstructionKind::B &&
            in_range(fallthrough, base_address, code.size())) {
            block.successors.push_back(fallthrough);
            if (!built.count(fallthrough) && queued.insert(fallthrough).second)
                worklist.push_back(fallthrough);
        }
        std::sort(block.successors.begin(), block.successors.end());
        block.successors.erase(std::unique(block.successors.begin(),
                                           block.successors.end()),
                               block.successors.end());
        built.insert(block.address);
        graph.blocks.push_back(std::move(block));
    }
    std::sort(graph.blocks.begin(), graph.blocks.end(),
              [](const BasicBlock &a, const BasicBlock &b) {
                  return a.address < b.address;
              });
    return graph;
}

std::string cfg_fingerprint(const ControlFlowGraph &graph) {
    uint64_t hash = 1469598103934665603ULL;
    auto add = [&](uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= (value >> (i * 8)) & 0xffU;
            hash *= 1099511628211ULL;
        }
    };
    add(graph.blocks.size());
    for (const auto &block : graph.blocks) {
        add(block.address);
        add(block.size);
        add(block.successors.size());
        for (auto successor : block.successors)
            add(successor);
    }
    char value[17];
    std::snprintf(value, sizeof(value), "%016llx", (unsigned long long)hash);
    return value;
}

static bool printable_string_at(const std::vector<uint8_t> &code,
                                uint64_t base_address,
                                uint64_t address) {
    if (!in_range(address, base_address, code.size())) return false;
    std::size_t offset = (std::size_t)(address - base_address);
    std::size_t length = 0;
    while (offset + length < code.size() && length < 256) {
        unsigned char c = code[offset + length];
        if (c == 0) return length >= 4;
        if (c < 0x20 || c > 0x7e) return false;
        ++length;
    }
    return false;
}

static void append_unique(std::vector<uint64_t> &values, uint64_t value) {
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

FunctionIR analyze_function(const std::vector<uint8_t> &code,
                            uint64_t base_address,
                            uint64_t entry_address,
                            std::size_t max_instructions) {
    FunctionIR function;
    function.address = entry_address;
    auto graph = analyze_cfg(code, base_address, entry_address, max_instructions);
    function.blocks = graph.blocks;
    for (const auto &block : graph.blocks) {
        function.end_address = std::max(function.end_address,
                                        block.address + block.size);
        for (std::size_t offset = 0; offset < block.size; offset += 4) {
            uint64_t address = block.address + offset;
            if (!in_range(address, base_address, code.size())) break;
            uint32_t instruction = read_word(code, (std::size_t)(address - base_address));
            auto decoded = decode(instruction, address);
            if (decoded.kind == InstructionKind::BL && decoded.has_target)
                append_unique(function.calls, decoded.target);
            if (decoded.kind == InstructionKind::ADR ||
                decoded.kind == InstructionKind::ADRP ||
                decoded.kind == InstructionKind::LdrLiteral) {
                if (!decoded.has_target) continue;
                append_unique(function.constants, decoded.target);
                if (printable_string_at(code, base_address, decoded.target))
                    append_unique(function.strings, decoded.target);
            }
            if (is_return(instruction))
                append_unique(function.returns, address);
        }
    }
    std::sort(function.calls.begin(), function.calls.end());
    std::sort(function.constants.begin(), function.constants.end());
    std::sort(function.strings.begin(), function.strings.end());
    std::sort(function.returns.begin(), function.returns.end());
    function.fingerprint = function_fingerprint(function);
    return function;
}

std::string function_fingerprint(const FunctionIR &function) {
    uint64_t hash = 1469598103934665603ULL;
    auto add = [&](uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= (value >> (i * 8)) & 0xffU;
            hash *= 1099511628211ULL;
        }
    };
    add(function.blocks.size());
    for (const auto &block : function.blocks) {
        add(block.size);
        add(block.successors.size());
        for (auto successor : block.successors) add(successor);
    }
    add(function.calls.size());
    for (auto value : function.calls) add(value);
    add(function.constants.size());
    for (auto value : function.constants) add(value);
    add(function.strings.size());
    for (auto value : function.strings) add(value);
    add(function.returns.size());
    char value[17];
    std::snprintf(value, sizeof(value), "%016llx", (unsigned long long)hash);
    return value;
}

}
}
