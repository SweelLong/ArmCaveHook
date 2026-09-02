#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <map>

struct RelocEntry;
class BinaryImage;

std::vector<std::pair<std::string, std::string>> list_available_symbols(
    const std::filesystem::path &binary_path);

uint64_t find_function_address(BinaryImage *binary, const std::string &query);

std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
resolve_plugin_relocs(
    const std::vector<uint8_t> &text,
    const std::vector<uint8_t> &extra,
    const std::vector<RelocEntry> &relocs,
    const std::map<std::string, int> &offsets,
    const std::filesystem::path &binary_path,
    uint64_t text_va,
    uint64_t data_va);
