#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Log
{
    void Initialize(const std::filesystem::path& path);
    void Write(const std::string& message);
    std::vector<std::string> Recent();
} // namespace Log
