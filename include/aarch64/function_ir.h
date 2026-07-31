#pragma once

#include "aarch64/cfg.h"
#include "binary_image.h"

#include <cstddef>
#include <vector>

namespace armcave {
namespace aarch64 {

FunctionIR analyze_function(const BinaryImage &binary,
                            uint64_t entry_address,
                            std::size_t max_instructions = 4096);
std::vector<FunctionIR> discover_functions(const BinaryImage &binary,
                                           std::size_t max_functions = 4096,
                                           std::size_t max_instructions = 4096);

}
}
