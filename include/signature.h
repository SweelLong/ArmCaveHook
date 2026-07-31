#pragma once

#include "binary_image.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct ByteSignature {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> mask;
};

ByteSignature parse_signature(const std::string &source);
std::vector<uint64_t> find_signature_matches(const BinaryImage &binary,
                                              const ByteSignature &signature);
uint64_t find_unique_signature(const BinaryImage &binary,
                               const std::string &source);
