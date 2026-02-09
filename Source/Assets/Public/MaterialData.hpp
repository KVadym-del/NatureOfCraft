#pragma once
#include "../../Core/Public/Core.hpp"

#include <string>

#include <DirectXMath.h>

NOC_SUPPRESS_DLL_WARNINGS

/// CPU-side material properties.
/// Data model is ready; GPU upload (descriptor sets, textures) is deferred.
struct NOC_EXPORT MaterialData
{
    std::string name{};

    DirectX::XMFLOAT4 albedoColor{1.0f, 1.0f, 1.0f, 1.0f};
    float roughness{0.5f};
    float metallic{0.0f};

    std::string albedoTexturePath{};
    std::string normalTexturePath{};
    std::string roughnessTexturePath{};
};

NOC_RESTORE_DLL_WARNINGS
