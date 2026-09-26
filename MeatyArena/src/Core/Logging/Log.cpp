#include "Log.h"

#include <Windows.h>

#include <deque>
#include <fstream>
#include <mutex>

namespace
{
    std::mutex mutex;
    std::filesystem::path path;
    std::deque<std::string> lines;
} // namespace

void Log::Initialize(const std::filesystem::path& outputPath)
{
    std::lock_guard<std::mutex> lock(mutex);
    path = outputPath;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
}

void Log::Write(const std::string& message)
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char stamp[32]{};
    sprintf_s(stamp, "%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);
    const std::string line = std::string(stamp) + "  " + message;
    std::lock_guard<std::mutex> lock(mutex);
    lines.push_back(line);
    if (lines.size() > 250)
        lines.pop_front();
    if (!path.empty())
    {
        std::ofstream file(path, std::ios::app);
        if (file)
            file << line << '\n';
    }
}

std::vector<std::string> Log::Recent()
{
    std::lock_guard<std::mutex> lock(mutex);
    return {lines.begin(), lines.end()};
}
