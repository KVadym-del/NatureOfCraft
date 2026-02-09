#pragma once
#include "../../Core/Public/Core.hpp"

#include <DirectXMath.h>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <entt/entity/entity.hpp>

/// Name identifier for an entity.
struct NameComponent
{
    std::string name;
};

/// 3D transform: position, rotation (quaternion), and uniform/non-uniform scale.
/// Computes a local matrix as S * R * T (scale, then rotate, then translate).
struct TransformComponent
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
    DirectX::XMFLOAT3 get_rotation_euler() const noexcept
    {
        float qx = rotation.x, qy = rotation.y, qz = rotation.z, qw = rotation.w;

        float sinp = 2.0f * (qw * qx + qy * qz);
        float cosp = 1.0f - 2.0f * (qx * qx + qy * qy);
        float pitch = std::atan2(sinp, cosp);

        float siny = 2.0f * (qw * qy - qz * qx);
        float yaw = (std::abs(siny) >= 1.0f) ? std::copysign(DirectX::XM_PIDIV2, siny) : std::asin(siny);

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

/// Parent-child hierarchy relationship.
/// Entities without a HierarchyComponent (or with parent == entt::null) are root-level.
struct HierarchyComponent
{
    entt::entity parent{entt::null};
    std::vector<entt::entity> children;
};

/// Mesh rendering component. Entities with this are visible geometry.
struct MeshComponent
{
    int32_t meshIndex{-1};
    int32_t materialIndex{0};
    std::string assetPath; // original asset path, used for serialization
};

/// Camera component. The entity with isActive=true is the rendering camera.
/// Stores orbit controller state directly so it can be serialized with the level.
struct CameraComponent
{
    float fov{45.0f}; // degrees
    float nearPlane{0.1f};
    float farPlane{1000.0f};
    bool isActive{false};

    // Orbit camera state
    DirectX::XMFLOAT3 target{0.0f, 1.0f, 0.0f};
    float distance{10.0f};
    float yaw{0.0f};   // radians
    float pitch{0.0f}; // radians
};

/// Cached world matrix, recomputed each frame by World::update_world_matrices().
struct WorldMatrixCache
{
    DirectX::XMFLOAT4X4 worldMatrix{};
};
