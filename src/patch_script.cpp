#include "patch_script.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace((unsigned char)value.front()))
        value.erase(value.begin());
    while (!value.empty() && std::isspace((unsigned char)value.back()))
        value.pop_back();
    return value;
}

std::string value_text(std::string value) {
    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return value.substr(1, value.size() - 2);
    if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
        return value.substr(1, value.size() - 2);
    if (value.size() >= 2 && value.front() == '[' && value.back() == ']')
        value = value.substr(1, value.size() - 2);
    return trim(value);
}

std::vector<std::string> list_value(std::string value) {
    value = trim(value);
    if (value.size() >= 2 && value.front() == '[' && value.back() == ']')
        value = value.substr(1, value.size() - 2);
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= value.size()) {
        size_t comma = value.find(',', start);
        auto item = value_text(value.substr(start, comma - start));
        if (!item.empty()) result.push_back(item);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return result;
}

}

PatchScript load_patch_script(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot read patch script: " + path.string());
    PatchScript script;
    script.path = std::filesystem::absolute(path);
    std::string section;
    PatchScriptHook *hook = nullptr;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        line = trim(line);
        if (line.empty()) continue;
        if (line == "[target]") {
            section = "target";
            hook = nullptr;
            continue;
        }
        if (line == "[[hook]]" || line == "[[hooks]]") {
            section = "hook";
            script.hooks.emplace_back();
            hook = &script.hooks.back();
            continue;
        }
        auto equal = line.find('=');
        if (equal == std::string::npos)
            throw std::runtime_error(path.string() + ": invalid line " +
                                     std::to_string(line_number));
        std::string key = trim(line.substr(0, equal));
        std::string value = value_text(line.substr(equal + 1));
        if (section == "target") {
            if (key == "binary" || key == "input") script.binary = value;
            else if (key == "output") script.output = value;
            else if (key == "plugins") script.plugins = value;
        } else if (section == "hook" && hook) {
            if (key == "function" || key == "symbol") hook->function = value;
            else if (key == "signature") hook->signature = value;
            else if (key == "class" || key == "objc_class") hook->objc_class = value;
            else if (key == "selector") hook->selector = value;
            else if (key == "replace") {
                hook->kind = "hook_replace";
                hook->source = value;
            } else if (key == "detour") {
                hook->kind = "hook_detour";
                hook->source = value;
            } else if (key == "source") hook->source = value;
            else if (key == "handler") hook->handler = value;
            else if (key == "registers" || key == "args")
                hook->register_args = list_value(line.substr(equal + 1));
        }
    }
    if (script.binary.empty())
        throw std::runtime_error("patch script has no target.binary");
    auto root = script.path.parent_path();
    if (script.binary.is_relative()) script.binary = root / script.binary;
    if (script.output.empty()) script.output = script.binary.string() + ".patched";
    else if (script.output.is_relative()) script.output = root / script.output;
    if (!script.plugins.empty() && script.plugins.is_relative())
        script.plugins = root / script.plugins;
    if (script.hooks.empty())
        throw std::runtime_error("patch script has no [[hook]] entries");
    for (const auto &hook_item : script.hooks) {
        if (hook_item.source.empty())
            throw std::runtime_error("patch script hook has no replace/detour source");
        if (hook_item.function.empty() && hook_item.signature.empty() &&
            (hook_item.objc_class.empty() || hook_item.selector.empty()))
            throw std::runtime_error("patch script hook has no function, signature, or Objective-C selector");
    }
    return script;
}
