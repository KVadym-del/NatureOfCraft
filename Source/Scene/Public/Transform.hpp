#pragma once
#include <DirectXMath.h>
#include <cmath>

/// A 3D transform: position, rotation (quaternion), and scale.
/// Computes a local matrix as S * R * T (scale, then rotate, then translate).
struct Transform
{
    DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 rotation{0.0f, 0.0f, 0.0f, 1.0f}; // quaternion (x, y, z, w) — identity
    DirectX::XMFLOAT3 scale{1.0f, 1.0f, 1.0f};

    /// Returns the local transform matrix: Scale * Rotation * Translation.
    DirectX::XMMATRIX get_local_matrix() const noexcept
    {
        using namespace DirectX;
        XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
        XMMATRIX R = XMMatrixRotationQuaternion(XMLoadFloat4(&rotation));
        XMMATRIX T = XMMatrixTranslation(position.x, position.y, position.z);
        return XMMatrixMultiply(XMMatrixMultiply(S, R), T);
    }

    /// Sets rotation from Euler angles (in radians). Order: pitch (X), yaw (Y), roll (Z).
    void set_rotation_euler(float pitch, float yaw, float roll) noexcept
    {
        using namespace DirectX;
        XMStoreFloat4(&rotation, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    }

    /// Returns rotation as Euler angles (in radians): pitch (X), yaw (Y), roll (Z).
    /// Uses the quaternion-to-Euler decomposition (may lose information at gimbal lock).
    DirectX::XMFLOAT3 get_rotation_euler() const noexcept
    {
        // Convert quaternion (x,y,z,w) to Euler angles (pitch, yaw, roll)
        float qx = rotation.x, qy = rotation.y, qz = rotation.z, qw = rotation.w;

        // Pitch (X-axis rotation)
        float sinp = 2.0f * (qw * qx + qy * qz);
        float cosp = 1.0f - 2.0f * (qx * qx + qy * qy);
        float pitch = std::atan2(sinp, cosp);

        // Yaw (Y-axis rotation)
        float siny = 2.0f * (qw * qy - qz * qx);
        float yaw = (std::abs(siny) >= 1.0f) ? std::copysign(DirectX::XM_PIDIV2, siny) : std::asin(siny);

        // Roll (Z-axis rotation)
        float sinr = 2.0f * (qw * qz + qx * qy);
        float cosr = 1.0f - 2.0f * (qy * qy + qz * qz);
        float roll = std::atan2(sinr, cosr);

        return {pitch, yaw, roll};
    }

    /// Sets rotation from an axis and angle (in radians).
    void set_rotation_axis(const DirectX::XMFLOAT3& axis, float angleRadians) noexcept
    {
        using namespace DirectX;
        XMStoreFloat4(&rotation, XMQuaternionRotationAxis(XMLoadFloat3(&axis), angleRadians));
    }
};
