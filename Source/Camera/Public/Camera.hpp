#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../ECS/Public/Components.hpp"

#include <DirectXMath.h>

#include <entt/entity/registry.hpp>

NOC_SUPPRESS_DLL_WARNINGS

/// Controller that drives an orbit camera by reading/writing the CameraComponent
/// on a given entity. The orbit state (target, distance, yaw, pitch, fov, planes)
/// lives in the CameraComponent so it can be serialized with the level.
/// The controller only owns transient tuning parameters (speeds, limits).
class NOC_EXPORT OrbitCameraController
{
  public:
    OrbitCameraController() = default;

    /// Attach the controller to a specific camera entity in the given registry.
    /// The entity must already have a CameraComponent.
    void attach(entt::registry& registry, entt::entity cameraEntity) noexcept;

    /// Detach from the current entity.
    void detach() noexcept;

    /// Returns true if the controller is attached to a valid entity.
    bool is_attached() const noexcept;

    // ── Input actions ─────────────────────────────────────────────────

    /// Rotates the camera around the target by delta pixels/units.
    void rotate(float deltaYaw, float deltaPitch) noexcept;

    /// Zooms by adjusting the distance to the target.
    void zoom(float delta) noexcept;

    /// Pans the camera target in the view-local right/up plane.
    void pan(float deltaRight, float deltaUp) noexcept;

    // ── Matrix computation ────────────────────────────────────────────

    /// Returns the view matrix (right-handed look-at).
    DirectX::XMMATRIX get_view_matrix() const noexcept;

    /// Returns the projection matrix (right-handed perspective, Vulkan Y-flip applied).
    DirectX::XMMATRIX get_projection_matrix(float aspectRatio) const noexcept;

    /// Returns the world-space eye position.
    DirectX::XMFLOAT3 get_eye_position() const noexcept;

    // ── Tuning parameters ─────────────────────────────────────────────

    void set_rotation_speed(float speed) noexcept
    {
        m_rotationSpeed = speed;
    }
    void set_zoom_speed(float speed) noexcept
    {
        m_zoomSpeed = speed;
    }
    void set_pan_speed(float speed) noexcept
    {
        m_panSpeed = speed;
    }

    float get_rotation_speed() const noexcept
    {
        return m_rotationSpeed;
    }
    float get_zoom_speed() const noexcept
    {
        return m_zoomSpeed;
    }
    float get_pan_speed() const noexcept
    {
        return m_panSpeed;
    }

  private:
    /// Returns the CameraComponent on the attached entity. UB if not attached.
    CameraComponent& get_component() noexcept;
    const CameraComponent& get_component() const noexcept;

    entt::registry* m_registry{nullptr};
    entt::entity m_entity{entt::null};

    float m_rotationSpeed{0.005f};
    float m_zoomSpeed{1.0f};
    float m_panSpeed{0.01f};

    static constexpr float kMinDistance{0.5f};
    static constexpr float kMaxDistance{500.0f};
    static constexpr float kMaxPitch{DirectX::XM_PIDIV2 - 0.01f};
};

NOC_RESTORE_DLL_WARNINGS
