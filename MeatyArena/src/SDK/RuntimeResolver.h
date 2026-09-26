#pragma once

#include <string>

class MemoryClient;

namespace RuntimeResolver
{
    bool Initialize(MemoryClient& memory, std::string& error);
} 
