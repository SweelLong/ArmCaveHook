#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct PatchScriptHook {
    std::string kind = "hook_replace";
    std::string function;
    std::string signature;
    std::string objc_class;
    std::string selector;
    std::string source;
    std::string handler = "replacement";
    std::vector<std::string> register_args;
};

struct PatchScript {
    std::filesystem::path path;
    std::filesystem::path binary;
    std::filesystem::path output;
    std::filesystem::path plugins;
    std::vector<PatchScriptHook> hooks;
};

PatchScript load_patch_script(const std::filesystem::path &path);
