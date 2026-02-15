#pragma once
#include "../../Core/Public/Core.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

NOC_SUPPRESS_DLL_WARNINGS

/// CPU-side texture pixel data.
/// Data model is ready; GPU upload (VkImage, VkSampler) is deferred.
struct NOC_EXPORT TextureData
{
    /// Human-readable texture identifier.
    std::string name{};
    /// Source asset path for this texture payload.
    std::filesystem::path sourcePath{};

    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t channels{};

    std::vector<uint8_t> pixels{};
};

NOC_RESTORE_DLL_WARNINGS
