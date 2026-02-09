#include "../Public/Camera.hpp"

#include <algorithm>
#include <cmath>

using namespace DirectX;

void OrbitCameraController::attach(entt::registry& registry, entt::entity cameraEntity) noexcept
{
    m_registry = &registry;
    m_entity = cameraEntity;
}

void OrbitCameraController::detach() noexcept
{
    m_registry = nullptr;
    m_entity = entt::null;
}

bool OrbitCameraController::is_attached() const noexcept
{
    return m_registry != nullptr && m_entity != entt::null && m_registry->valid(m_entity);
}

CameraComponent& OrbitCameraController::get_component() noexcept
{
    return m_registry->get<CameraComponent>(m_entity);
}

const CameraComponent& OrbitCameraController::get_component() const noexcept
{
    return m_registry->get<CameraComponent>(m_entity);
}

void OrbitCameraController::rotate(float deltaYaw, float deltaPitch) noexcept
{
    if (!is_attached())
        return;

    auto& cam = get_component();
    cam.yaw += deltaYaw * m_rotationSpeed;
    cam.pitch = std::clamp(cam.pitch + deltaPitch * m_rotationSpeed, -kMaxPitch, kMaxPitch);
}

void OrbitCameraController::zoom(float delta) noexcept
{
    if (!is_attached())
        return;

    auto& cam = get_component();
    cam.distance = std::clamp(cam.distance - delta * m_zoomSpeed, kMinDistance, kMaxDistance);
}

void OrbitCameraController::pan(float deltaRight, float deltaUp) noexcept
{
    if (!is_attached())
        return;

    auto& cam = get_component();

    float cosP = std::cos(cam.pitch);
    float sinP = std::sin(cam.pitch);
    float cosY = std::cos(cam.yaw);
    float sinY = std::sin(cam.yaw);

    // Forward direction (from target toward eye)
    XMFLOAT3 forward{cosP * sinY, sinP, cosP * cosY};

    XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR fwd = XMLoadFloat3(&forward);

    // Right = normalize(cross(worldUp, forward))
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, fwd));

    // Up = cross(forward, right)
    XMVECTOR up = XMVector3Cross(fwd, right);

    // Scale and apply to target
    XMVECTOR target = XMLoadFloat3(&cam.target);
    target = XMVectorAdd(target, XMVectorScale(right, -deltaRight * m_panSpeed));
    target = XMVectorAdd(target, XMVectorScale(up, deltaUp * m_panSpeed));
    XMStoreFloat3(&cam.target, target);
}

XMFLOAT3 OrbitCameraController::get_eye_position() const noexcept
{
    if (!is_attached())
        return {0.0f, 0.0f, 0.0f};

    const auto& cam = get_component();

    float cosP = std::cos(cam.pitch);
    float sinP = std::sin(cam.pitch);
    float cosY = std::cos(cam.yaw);
    float sinY = std::sin(cam.yaw);

    XMFLOAT3 eye;
    eye.x = cam.target.x + cosP * sinY * cam.distance;
    eye.y = cam.target.y + sinP * cam.distance;
    eye.z = cam.target.z + cosP * cosY * cam.distance;
    return eye;
}

XMMATRIX OrbitCameraController::get_view_matrix() const noexcept
{
    if (!is_attached())
        return XMMatrixIdentity();

    const auto& cam = get_component();

    XMFLOAT3 eye = get_eye_position();
    XMVECTOR eyeVec = XMLoadFloat3(&eye);
    XMVECTOR targetVec = XMLoadFloat3(&cam.target);
    XMVECTOR upVec = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    return XMMatrixLookAtRH(eyeVec, targetVec, upVec);
}

XMMATRIX OrbitCameraController::get_projection_matrix(float aspectRatio) const noexcept
{
    if (!is_attached())
        return XMMatrixIdentity();

    const auto& cam = get_component();

    float fovRadians = XMConvertToRadians(cam.fov);
    XMMATRIX proj = XMMatrixPerspectiveFovRH(fovRadians, aspectRatio, cam.nearPlane, cam.farPlane);

    // Vulkan clip space Y-flip
    XMMATRIX vulkanFlip = XMMatrixScaling(1.0f, -1.0f, 1.0f);
    return XMMatrixMultiply(proj, vulkanFlip);
}
