#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>

struct HookAction {
    std::string kind;
    uint64_t address = 0;
    std::string signature;
    std::string symbol;
    std::string objc_class;
    std::string selector;
    std::string swift_name;
    std::string handler;
    std::string function_name;
    std::string segment = "auto";
    std::vector<std::string> register_args;
    int size = 0;
    std::string data;
    std::string expected;
    bool has_expected = false;
    int asm_offset = 0;
};

struct PluginSpec {
    std::filesystem::path path;
    std::string name;
    std::vector<HookAction> actions;

    std::string summary() const {
        std::string out;
        for (size_t i = 0; i < actions.size(); i++) {
            if (i > 0) out += ", ";
            auto &a = actions[i];
            out += a.kind + ":";
            if (a.address == 0 && !a.handler.empty())
                out += "entry";
            else {
                char buf[32];
                snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)a.address);
                out += buf;
            }
        }
        return out;
    }
};

inline PluginSpec load_plugin(const std::filesystem::path &path) {
    return PluginSpec{path, path.stem().string(), {}};
}
