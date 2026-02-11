#pragma once

#include <DirectXMath.h>
#include <cmath>

/// Lightweight Vec3 for Lua scripts. Wraps DirectX::XMFLOAT3 with arithmetic operators.
struct LuaVec3
{
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    LuaVec3() = default;
    LuaVec3(float x, float y, float z) : x(x), y(y), z(z)
    {}
    explicit LuaVec3(const DirectX::XMFLOAT3& v) : x(v.x), y(v.y), z(v.z)
    {}

    DirectX::XMFLOAT3 to_dx() const noexcept
    {
        return {x, y, z};
    }

    LuaVec3 operator+(const LuaVec3& o) const noexcept
    {
        return {x + o.x, y + o.y, z + o.z};
    }
    LuaVec3 operator-(const LuaVec3& o) const noexcept
    {
        return {x - o.x, y - o.y, z - o.z};
    }
    LuaVec3 operator*(float s) const noexcept
    {
        return {x * s, y * s, z * s};
    }
    LuaVec3 operator-() const noexcept
    {
        return {-x, -y, -z};
    }

    float length() const noexcept
    {
        return std::sqrt(x * x + y * y + z * z);
    }

    LuaVec3 normalized() const noexcept
    {
        float len = length();
        if (len < 1e-8f)
            return {0.0f, 0.0f, 0.0f};
        return {x / len, y / len, z / len};
    }

    float dot(const LuaVec3& o) const noexcept
    {
        return x * o.x + y * o.y + z * o.z;
    }

    LuaVec3 cross(const LuaVec3& o) const noexcept
    {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
};
