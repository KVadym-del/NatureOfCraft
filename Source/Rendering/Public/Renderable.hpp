#pragma once
#include <cstdint>

#include <DirectXMath.h>

/// A renderable instance: a world transform paired with mesh and material indices.
/// This is the output of Scene::collect_renderables() and the input to the renderer.
struct Renderable
{
    DirectX::XMFLOAT4X4 worldMatrix{};
    uint32_t meshIndex{};
    uint32_t materialIndex{};
    uint32_t entityId{};
    DirectX::XMFLOAT3 glowColor{1.0f, 0.7f, 0.25f};
    float glowIntensity{0.0f};
};
