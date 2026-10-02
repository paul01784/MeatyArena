#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace Log
{
    void Initialize(const std::filesystem::path& path);
    void Write(const std::string& message);
    bool UpdateRecent(std::uint64_t& revision, std::vector<std::string>& output);
} // namespace Log
