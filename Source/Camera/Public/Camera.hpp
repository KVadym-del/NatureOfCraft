#pragma once
#include "../../Core/Public/Core.hpp"

#include <DirectXMath.h>

NOC_SUPPRESS_DLL_WARNINGS

class NOC_EXPORT OrbitCamera
{
  public:
    OrbitCamera() = default;

    OrbitCamera(const DirectX::XMFLOAT3& target, float distance, float yawDegrees, float pitchDegrees)
        : m_target(target), m_distance(distance), m_yaw(DirectX::XMConvertToRadians(yawDegrees)),
          m_pitch(DirectX::XMConvertToRadians(pitchDegrees))
    {}

    /// Rotates the camera around the target by delta pixels/units.
    /// Deltas are scaled internally by m_rotationSpeed.
    void rotate(float deltaYaw, float deltaPitch) noexcept;

    /// Zooms the camera by adjusting the distance to the target.
    /// Positive delta zooms in, negative zooms out.
    void zoom(float delta) noexcept;

    /// Pans the camera target in the view-local right/up plane.
    /// Deltas are scaled internally by m_panSpeed.
    void pan(float deltaRight, float deltaUp) noexcept;

    /// Returns the view matrix (right-handed).
    DirectX::XMMATRIX get_view_matrix() const noexcept;

    /// Returns the projection matrix (right-handed, with Vulkan Y-flip applied).
    DirectX::XMMATRIX get_projection_matrix(float aspectRatio) const noexcept;

    /// Returns the world-space eye position.
    DirectX::XMFLOAT3 get_eye_position() const noexcept;

  public:
    void set_target(const DirectX::XMFLOAT3& target) noexcept
    {
        m_target = target;
    }
    void set_distance(float distance) noexcept;
    void set_fov_degrees(float fovDegrees) noexcept
    {
        m_fov = DirectX::XMConvertToRadians(fovDegrees);
    }
    void set_near_plane(float nearPlane) noexcept
    {
        m_nearPlane = nearPlane;
    }
    void set_far_plane(float farPlane) noexcept
    {
        m_farPlane = farPlane;
    }
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

    const DirectX::XMFLOAT3& get_target() const noexcept
    {
        return m_target;
    }
    float get_distance() const noexcept
    {
        return m_distance;
    }
    float get_fov_degrees() const noexcept
    {
        return DirectX::XMConvertToDegrees(m_fov);
    }
    float get_yaw() const noexcept
    {
        return m_yaw;
    }
    float get_pitch() const noexcept
    {
        return m_pitch;
    }
    float get_near_plane() const noexcept
    {
        return m_nearPlane;
    }
    float get_far_plane() const noexcept
    {
        return m_farPlane;
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
    DirectX::XMFLOAT3 m_target{0.0f, 1.0f, 0.0f};
    float m_distance{10.0f};

    float m_yaw{0.0f};
    float m_pitch{0.0f};

    float m_fov{DirectX::XMConvertToRadians(45.0f)};
    float m_nearPlane{0.1f};
    float m_farPlane{1000.0f};

    float m_rotationSpeed{0.005f};
    float m_zoomSpeed{1.0f};
    float m_panSpeed{0.01f};

    static constexpr float kMinDistance{0.5f};
    static constexpr float kMaxDistance{500.0f};
    static constexpr float kMaxPitch{DirectX::XM_PIDIV2 - 0.01f};
};

NOC_RESTORE_DLL_WARNINGS
