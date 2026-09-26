#pragma once

#include "../App/AppConfig.h"

#include <filesystem>
#include <string>

class ConfigStore
{
public:
    explicit ConfigStore(std::filesystem::path path);

    bool Load(AppConfig& config, std::string& error) const;
    bool Save(const AppConfig& config, std::string& error) const;
    const std::filesystem::path& GetPath() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};
