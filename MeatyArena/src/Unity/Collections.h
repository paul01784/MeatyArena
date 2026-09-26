#pragma once

#include "../Memory/MemoryClient.h"
#include "Types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Unity::Collections
{
    template <typename T> bool ReadArray(const MemoryClient& memory, std::uint64_t array, std::vector<T>& values, int maxCount = 16384)
    {
        values.clear();
        std::int32_t count = 0;
        if (!IsValidAddress(array) || !memory.TryRead(array + 0x18, count) || count < 0 || count > maxCount)
            return false;
        values.resize(count);
        if (count && !memory.Read(array + 0x20, values.data(), sizeof(T) * values.size()))
        {
            values.clear();
            return false;
        }
        return true;
    }

    template <typename T> bool ReadList(const MemoryClient& memory, std::uint64_t list, std::vector<T>& values, int maxCount = 16384)
    {
        values.clear();
        std::int32_t count = 0;
        std::uint64_t array = 0;
        if (!IsValidAddress(list) || !memory.TryRead(list + 0x18, count) || count < 0 || count > maxCount || !memory.TryRead(list + 0x10, array) || !IsValidAddress(array))
            return false;
        values.resize(count);
        if (count && !memory.Read(array + 0x20, values.data(), sizeof(T) * values.size()))
        {
            values.clear();
            return false;
        }
        return true;
    }

    // IL2CPP Dictionary count/entries fields
    template <typename K, typename V> bool ReadDictionary(const MemoryClient& memory, std::uint64_t dictionary, std::vector<std::pair<K, V>>& values, int maxCount = 16384)
    {
        values.clear();
        std::int32_t count = 0;
        std::uint64_t entries = 0;
        if (!IsValidAddress(dictionary) || !memory.TryRead(dictionary + 0x20, count) || count < 0 || count > maxCount || !memory.TryRead(dictionary + 0x18, entries) ||
            !IsValidAddress(entries))
            return false;
        struct Entry
        {
            std::int32_t hashCode;
            std::int32_t next;
            K key;
            V value;
        };
        std::vector<Entry> raw(count);
        if (count && !memory.Read(entries + 0x20, raw.data(), sizeof(Entry) * raw.size()))
            return false;
        for (const auto& entry : raw)
            if (entry.hashCode >= 0)
                values.emplace_back(entry.key, entry.value);
        return true;
    }
}