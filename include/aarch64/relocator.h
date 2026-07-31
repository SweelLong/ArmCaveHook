#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace armcave {
namespace aarch64 {

struct RelocationOptions {
    uint8_t scratch_register = 16;
};

struct RelocationResult {
    std::vector<uint8_t> bytes;
    std::size_t instruction_count = 0;
};

RelocationResult relocate_block(const std::vector<uint8_t> &code,
                                uint64_t source_address,
                                uint64_t destination_address,
                                const RelocationOptions &options = {});

std::size_t max_relocated_size(std::size_t input_size);

}
}
