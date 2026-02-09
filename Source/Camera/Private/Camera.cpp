#include "../Public/Camera.hpp"

#include <algorithm>
#include <cmath>

using namespace DirectX;

void OrbitCamera::rotate(float deltaYaw, float deltaPitch) noexcept
{
    m_yaw += deltaYaw * m_rotationSpeed;
    m_pitch = std::clamp(m_pitch + deltaPitch * m_rotationSpeed, -kMaxPitch, kMaxPitch);
}

void OrbitCamera::zoom(float delta) noexcept
{
    m_distance = std::clamp(m_distance - delta * m_zoomSpeed, kMinDistance, kMaxDistance);
}

void OrbitCamera::pan(float deltaRight, float deltaUp) noexcept
{
    // Compute view-local right and up vectors from yaw/pitch
    float cosP = std::cos(m_pitch);
    float sinP = std::sin(m_pitch);
    float cosY = std::cos(m_yaw);
    float sinY = std::sin(m_yaw);

    // Forward direction (from target toward eye)
    XMFLOAT3 forward{cosP * sinY, sinP, cosP * cosY};

    // World up
    XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR fwd = XMLoadFloat3(&forward);

    // Right = normalize(cross(worldUp, forward))
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, fwd));

    // Up = cross(forward, right)
    XMVECTOR up = XMVector3Cross(fwd, right);

    // Scale and apply to target
    XMVECTOR target = XMLoadFloat3(&m_target);
    target = XMVectorAdd(target, XMVectorScale(right, -deltaRight * m_panSpeed));
    target = XMVectorAdd(target, XMVectorScale(up, deltaUp * m_panSpeed));
    XMStoreFloat3(&m_target, target);
}

DirectX::XMFLOAT3 OrbitCamera::get_eye_position() const noexcept
{
    float cosP = std::cos(m_pitch);
    float sinP = std::sin(m_pitch);
    float cosY = std::cos(m_yaw);
    float sinY = std::sin(m_yaw);

    XMFLOAT3 eye;
    eye.x = m_target.x + cosP * sinY * m_distance;
    eye.y = m_target.y + sinP * m_distance;
    eye.z = m_target.z + cosP * cosY * m_distance;
    return eye;
}

DirectX::XMMATRIX OrbitCamera::get_view_matrix() const noexcept
{
    XMFLOAT3 eye = get_eye_position();
    XMVECTOR eyeVec = XMLoadFloat3(&eye);
    XMVECTOR targetVec = XMLoadFloat3(&m_target);
    XMVECTOR upVec = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    return XMMatrixLookAtRH(eyeVec, targetVec, upVec);
}

DirectX::XMMATRIX OrbitCamera::get_projection_matrix(float aspectRatio) const noexcept
{
    XMMATRIX proj = XMMatrixPerspectiveFovRH(m_fov, aspectRatio, m_nearPlane, m_farPlane);

    // Vulkan clip space Y-flip
    XMMATRIX vulkanFlip = XMMatrixScaling(1.0f, -1.0f, 1.0f);
    return XMMatrixMultiply(proj, vulkanFlip);
}

void OrbitCamera::set_distance(float distance) noexcept
{
    m_distance = std::clamp(distance, kMinDistance, kMaxDistance);
}
