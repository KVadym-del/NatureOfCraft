#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Rendering/Public/Mesh.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <DirectXMath.h>

NOC_SUPPRESS_DLL_WARNINGS

/// CPU-side mesh data: vertices, indices, bounding box.
/// This is what gets cached in the AssetManager and serialized via FlatBuffers.
/// The renderer uploads this to GPU buffers separately.
struct NOC_EXPORT MeshData
{
    std::string name{};
    std::string sourcePath{};

    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};

    DirectX::XMFLOAT3 boundsMin{};
    DirectX::XMFLOAT3 boundsMax{};

    /// Recompute the axis-aligned bounding box from the current vertex data.
    void compute_bounds() noexcept
    {
        using namespace DirectX;
        if (vertices.empty())
        {
            boundsMin = {0.0f, 0.0f, 0.0f};
            boundsMax = {0.0f, 0.0f, 0.0f};
            return;
        }

        XMVECTOR vMin = XMLoadFloat3(&vertices[0].pos);
        XMVECTOR vMax = vMin;

        for (size_t i = 1; i < vertices.size(); ++i)
        {
            XMVECTOR v = XMLoadFloat3(&vertices[i].pos);
            vMin = XMVectorMin(vMin, v);
            vMax = XMVectorMax(vMax, v);
        }

        XMStoreFloat3(&boundsMin, vMin);
        XMStoreFloat3(&boundsMax, vMax);
    }
};

NOC_RESTORE_DLL_WARNINGS
