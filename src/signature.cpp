#include "signature.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace {

uint8_t hex_digit(char value) {
    if (value >= '0' && value <= '9') return (uint8_t)(value - '0');
    if (value >= 'a' && value <= 'f') return (uint8_t)(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return (uint8_t)(value - 'A' + 10);
    throw std::runtime_error("invalid signature hex digit");
}

bool is_wildcard(const std::string &token) {
    return token == "?" || token == "??" || token == "*";
}

void append_token(ByteSignature &result, const std::string &token) {
    if (is_wildcard(token)) {
        result.bytes.push_back(0);
        result.mask.push_back(0);
        return;
    }
    std::string value = token;
    if (value.size() > 2 && value[0] == '0' &&
        (value[1] == 'x' || value[1] == 'X'))
        value.erase(0, 2);
    if (value.size() != 2)
        throw std::runtime_error("signature bytes must contain two hex digits");
    result.bytes.push_back((uint8_t)((hex_digit(value[0]) << 4) |
                                     hex_digit(value[1])));
    result.mask.push_back(0xff);
}

std::vector<std::pair<uint64_t, uint64_t>> executable_ranges(
    const BinaryImage &binary) {
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    for (const auto &section : binary.sections()) {
        bool executable = section.name == "__text" || section.name == ".text" ||
                          section.name.find("text") != std::string::npos;
        if (executable && section.size && section.offset < binary.data().size())
            ranges.push_back({section.virtual_address, section.size});
    }
    if (!ranges.empty()) return ranges;
    for (const auto &segment : binary.segments()) {
        if ((segment.init_protection & 1U) && segment.file_size)
            ranges.push_back({segment.virtual_address, segment.file_size});
    }
    return ranges;
}

}

ByteSignature parse_signature(const std::string &source) {
    ByteSignature result;
    std::string token;
    for (size_t i = 0; i <= source.size(); ++i) {
        char c = i < source.size() ? source[i] : ' ';
        if (std::isspace((unsigned char)c) || c == ',' || c == ';') {
            if (!token.empty()) {
                append_token(result, token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (result.bytes.empty())
        throw std::runtime_error("empty AArch64 signature");
    return result;
}

std::vector<uint64_t> find_signature_matches(const BinaryImage &binary,
                                              const ByteSignature &signature) {
    if (signature.bytes.empty() || signature.bytes.size() != signature.mask.size())
        throw std::runtime_error("invalid AArch64 signature");
    std::vector<uint64_t> matches;
    const auto &data = binary.data();
    for (const auto &[va, size] : executable_ranges(binary)) {
        auto offset = binary.virtual_address_to_offset(va);
        if (!offset || *offset >= data.size()) continue;
        size_t available = std::min<uint64_t>(size, data.size() - *offset);
        if (available < signature.bytes.size()) continue;
        for (size_t i = 0; i + signature.bytes.size() <= available; ++i) {
            bool matched = true;
            for (size_t j = 0; j < signature.bytes.size(); ++j) {
                if (signature.mask[j] &&
                    (data[*offset + i + j] & signature.mask[j]) !=
                    (signature.bytes[j] & signature.mask[j])) {
                    matched = false;
                    break;
                }
            }
            if (matched) matches.push_back(va + i);
        }
    }
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    return matches;
}

uint64_t find_unique_signature(const BinaryImage &binary,
                               const std::string &source) {
    auto matches = find_signature_matches(binary, parse_signature(source));
    if (matches.empty())
        throw std::runtime_error("signature did not match: " + source);
    if (matches.size() != 1)
        throw std::runtime_error("signature is ambiguous (" +
                                 std::to_string(matches.size()) + " matches): " +
                                 source);
    return matches.front();
}
