#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace armcave {
namespace aarch64 {

struct BasicBlock {
    uint64_t address = 0;
    std::size_t size = 0;
    std::vector<uint64_t> successors;
};

struct ControlFlowGraph {
    std::vector<BasicBlock> blocks;
};

struct FunctionIR {
    uint64_t address = 0;
    uint64_t end_address = 0;
    std::vector<BasicBlock> blocks;
    std::vector<uint64_t> calls;
    std::vector<uint64_t> strings;
    std::vector<uint64_t> constants;
    std::vector<uint64_t> returns;
    std::string fingerprint;
};

ControlFlowGraph analyze_cfg(const std::vector<uint8_t> &code,
                             uint64_t base_address,
                             uint64_t entry_address,
                             std::size_t max_instructions = 4096);
FunctionIR analyze_function(const std::vector<uint8_t> &code,
                            uint64_t base_address,
                            uint64_t entry_address,
                            std::size_t max_instructions = 4096);
std::string cfg_fingerprint(const ControlFlowGraph &graph);
std::string function_fingerprint(const FunctionIR &function);

}
}
