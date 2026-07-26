#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

struct PluginBlob;

uint32_t encode_b(uint64_t src_va, uint64_t dst_va);
uint32_t encode_bl(uint64_t src_va, uint64_t dst_va);

int target_insn(uint32_t insn, uint64_t va);
int hook_dispatch_size(int handler_count, int original_size, bool override_original,
                       bool strip_pac);

std::vector<uint8_t> build_hook_dispatch(
    uint64_t cave_va,
    uint64_t hook_va,
    int hook_size,
    const std::vector<uint8_t> &original,
    const std::vector<uint64_t> &handler_vas,
    bool override_original,
    bool strip_pac);

int plugin_wrapper_size(const std::vector<std::string> &registers);
std::vector<uint8_t> build_plugin_wrapper(
    const std::vector<std::string> &registers,
    uint64_t wrapper_va,
    uint64_t plugin_va);

std::vector<uint8_t> build_hook_cave(
    uint64_t cave_va,
    uint64_t hook_va,
    int hook_size,
    const std::vector<uint8_t> &original,
    const std::vector<PluginBlob *> &plugin_blobs,
    const std::filesystem::path *target_binary,
    bool override_original = false,
    bool branch_host = false,
    const std::vector<uint64_t> *nop_addrs = nullptr);

void patch_hook_window(const std::filesystem::path &binary_path,
                       const std::filesystem::path &output_path,
                       uint64_t src_va, int size, uint64_t dst_va);

void patch_bytes_va(const std::filesystem::path &binary_path,
                    const std::filesystem::path &output_path,
                    uint64_t va, const std::vector<uint8_t> &payload);

std::pair<uint64_t, uint64_t> patch_hook_macho(
    const std::filesystem::path &binary_path,
    const std::filesystem::path &output_path,
    int hook_file_off, int hook_size,
    const std::vector<uint8_t> &original,
    const std::vector<PluginBlob *> &plugin_blobs,
    const std::string &seg_name,
    bool override_original = false, bool branch_host = false,
    const std::vector<uint64_t> *nop_addrs = nullptr);
