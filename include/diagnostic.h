#pragma once

#include <cstdint>
#include <string>

void diagnostic_error(const std::string &stage, const std::string &message,
                      uint64_t address = 0, const std::string &type = {},
                      const std::string &context = {});
void diagnostic_warning(const std::string &stage, const std::string &message,
                        uint64_t address = 0, const std::string &type = {},
                        const std::string &context = {});
