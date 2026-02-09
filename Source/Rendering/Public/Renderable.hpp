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
};
