#pragma once
#include <filesystem>
#include <vector>
#include <string>

void run_pipeline(const std::filesystem::path &input_path,
                  const std::filesystem::path &output_path,
                  const std::filesystem::path &plugins_dir,
                  const std::vector<std::string> *plugin_names = nullptr,
                  const std::string *whitelist = nullptr,
                  const std::string *blacklist = nullptr);

void run_patch_script(const std::filesystem::path &script_path);
