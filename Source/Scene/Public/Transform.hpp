#pragma once
#include <DirectXMath.h>

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

    /// Sets rotation from an axis and angle (in radians).
    void set_rotation_axis(const DirectX::XMFLOAT3& axis, float angleRadians) noexcept
    {
        using namespace DirectX;
        XMStoreFloat4(&rotation, XMQuaternionRotationAxis(XMLoadFloat3(&axis), angleRadians));
    }
};
