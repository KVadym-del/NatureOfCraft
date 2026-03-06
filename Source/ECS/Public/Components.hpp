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
    /// Uses YXZ decomposition to match XMQuaternionRotationRollPitchYaw(pitch, yaw, roll).
    /// Pitch (X) is the middle axis → constrained to ±90° (gimbal-lock axis).
    /// Yaw (Y) and Roll (Z) have the full ±180° range.
    DirectX::XMFLOAT3 get_rotation_euler() const noexcept
    {
        float qx = rotation.x, qy = rotation.y, qz = rotation.z, qw = rotation.w;

        // Pitch (X) — middle axis, use asin, range ±π/2
        float sinp = 2.0f * (qw * qx - qy * qz);
        float pitch = (std::abs(sinp) >= 1.0f) ? std::copysign(DirectX::XM_PIDIV2, sinp) : std::asin(sinp);

        // Yaw (Y) — use atan2, range ±π
        float siny = 2.0f * (qw * qy + qx * qz);
        float cosy = 1.0f - 2.0f * (qx * qx + qy * qy);
        float yaw = std::atan2(siny, cosy);

        // Roll (Z) — use atan2, range ±π
        float sinr = 2.0f * (qw * qz + qx * qy);
        float cosr = 1.0f - 2.0f * (qx * qx + qz * qz);
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
    std::string materialName; // name of the assigned editor material
    bool glowEnabled{false};
    DirectX::XMFLOAT3 glowColor{1.0f, 0.7f, 0.25f};
    float glowIntensity{1.5f};
    bool outlineEnabled{false};
    bool outlineThroughWalls{false};
    DirectX::XMFLOAT3 outlineColor{0.2f, 0.9f, 1.0f};
    float outlineThickness{2.0f};
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

/// Lua script attached to an entity.
/// The script file must define on_start(entity), on_update(entity, dt),
/// and/or on_destroy(entity) functions.
struct ScriptComponent
{
    std::string scriptPath;  // project-relative path, e.g. "Assets/Scripts/spin.lua"
    bool initialized{false}; // has on_start() been called?
};

enum class PhysicsBodyMotionType : std::uint8_t
{
    Static = 0,
    Dynamic = 1,
    Kinematic = 2,
};

enum class PhysicsColliderShapeType : std::uint8_t
{
    Box = 0,
    Sphere = 1,
    Capsule = 2,
    Cylinder = 3,
    Mesh = 4,
    ConvexHull = 5,
    StaticCompound = 6,
    MutableCompound = 7,
};

struct PhysicsBodyComponent
{
    bool enabled{true};
    PhysicsBodyMotionType motionType{PhysicsBodyMotionType::Dynamic};
    PhysicsColliderShapeType shapeType{PhysicsColliderShapeType::Box};
    DirectX::XMFLOAT3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius{0.5f};
    float halfHeight{0.5f};
    DirectX::XMFLOAT3 colliderOffset{0.0f, 0.0f, 0.0f};
    float friction{0.5f};
    float restitution{0.0f};
    bool useGravity{true};
    float linearDamping{0.05f};
    float angularDamping{0.05f};

    bool runtimeDirty{true};
    bool runtimeInitialized{false};
};
