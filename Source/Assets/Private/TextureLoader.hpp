#pragma once
#include "../../Core/Public/Expected.hpp"
#include "../Public/TextureData.hpp"

#include <memory>
#include <string_view>

/// Loads texture pixel data from image files (PNG, JPG, TGA, BMP, etc.) via stb_image.
/// Conforms to the EnTT resource_cache loader concept:
///   operator()(args...) -> shared_ptr<TextureData>
///
/// All images are forced to RGBA8 (4 channels) regardless of source format.
///
/// Usage with entt::resource_cache:
///   entt::resource_cache<TextureData, TextureLoader> cache;
///   cache.load(id, "path/to/texture.png");
struct NOC_EXPORT TextureLoader
{
    using result_type = std::shared_ptr<TextureData>;

    /// Load a texture from the given file path.
    /// Forces RGBA8 output (4 channels, 8 bits per channel).
    /// Returns nullptr on failure (EnTT cache convention — load silently fails).
    result_type operator()(const std::filesystem::path& path) const;

    /// Wrap pre-built TextureData into a shared_ptr (for programmatic textures).
    result_type operator()(const TextureData& data) const;

    /// Load a texture with full error reporting.
    static Result<std::shared_ptr<TextureData>> load_image(const std::filesystem::path& path);
};
