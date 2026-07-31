#pragma once

#include "binary_image.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace armcave {

struct ObjCMethodMetadata {
    std::string class_name;
    std::string selector;
    std::string types;
    uint64_t address = 0;
    uint64_t method_list = 0;
};

struct SwiftMetadataEntry {
    std::string name;
    uint64_t address = 0;
};

std::vector<ObjCMethodMetadata> enumerate_objc_methods(const BinaryImage &binary);
std::optional<uint64_t> find_objc_method(const BinaryImage &binary,
                                         const std::string &class_name,
                                         const std::string &selector);
std::vector<SwiftMetadataEntry> enumerate_swift_metadata(const BinaryImage &binary);
std::optional<uint64_t> find_swift_metadata(const BinaryImage &binary,
                                            const std::string &name);

}
