#pragma once

#include <cmath>
#include <cstdint>

namespace Unity
{
    struct Vector2
    {
        float x = 0.0f, y = 0.0f;
    };
    struct Vector3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        Vector3 operator+(const Vector3& other) const
        {
            return {x + other.x, y + other.y, z + other.z};
        }
        Vector3 operator-(const Vector3& other) const
        {
            return {x - other.x, y - other.y, z - other.z};
        }
        Vector3 operator*(float scale) const
        {
            return {x * scale, y * scale, z * scale};
        }
        float LengthSquared() const
        {
            return x * x + y * y + z * z;
        }
        bool IsFinite() const
        {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
        }
    };
    struct Quaternion
    {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    };
    struct Matrix4x4
    {
        float m[4][4]{};
    };

    inline bool IsValidAddress(std::uint64_t address)
    {
        return address > 0xFFFFFF && address < 0x0000800000000000ULL;
    }
} 