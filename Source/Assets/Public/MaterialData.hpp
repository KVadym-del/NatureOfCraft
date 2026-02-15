#pragma once
#include "../../Core/Public/Core.hpp"

#include <filesystem>
#include <string>

#include <DirectXMath.h>

NOC_SUPPRESS_DLL_WARNINGS

/// CPU-side material properties.
/// Data model is ready; GPU upload (descriptor sets, textures) is deferred.
struct NOC_EXPORT MaterialData
{
    /// Material identifier from source asset.
    std::string name{};

    /// Base color factor (RGBA).
    DirectX::XMFLOAT4 albedoColor{1.0f, 1.0f, 1.0f, 1.0f};
    /// Perceptual roughness in [0, 1].
    float roughness{0.5f};
    /// Metallic factor in [0, 1].
    float metallic{0.0f};

    /// Optional albedo texture path.
    std::filesystem::path albedoTexturePath{};
    /// Optional normal texture path.
    std::filesystem::path normalTexturePath{};
    /// Optional roughness texture path.
    std::filesystem::path roughnessTexturePath{};
};

NOC_RESTORE_DLL_WARNINGS
