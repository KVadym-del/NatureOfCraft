#pragma once
#include "../../Core/Public/Core.hpp"

#include <cstdint>
#include <string>
#include <vector>

NOC_SUPPRESS_DLL_WARNINGS

/// CPU-side texture pixel data.
/// Data model is ready; GPU upload (VkImage, VkSampler) is deferred.
struct NOC_EXPORT TextureData
{
    std::string name{};
    std::string sourcePath{};

    uint32_t width{};
    uint32_t height{};
    uint32_t channels{};

    std::vector<uint8_t> pixels{};
};

NOC_RESTORE_DLL_WARNINGS
