#include "TextureLoader.hpp"

#include <fmt/core.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdint>
#include <filesystem>

TextureLoader::result_type TextureLoader::operator()(const std::filesystem::path& path) const
{
    auto result = load_image(path);
    if (!result)
    {
        fmt::print(stderr, "TextureLoader: failed to load '{}': {}\n", path.string(), result.error().message);
        return nullptr;
    }
    return std::move(result.value());
}

TextureLoader::result_type TextureLoader::operator()(const TextureData& data) const
{
    return std::make_shared<TextureData>(data);
}

Result<std::shared_ptr<TextureData>> TextureLoader::load_image(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        return make_error(fmt::format("Texture file not found: {}", path.string()), ErrorCode::AssetFileNotFound);
    }

    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t channelsInFile = 0;
    constexpr std::int32_t desiredChannels = 4;

    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channelsInFile, desiredChannels);
    if (!pixels)
    {
        return make_error(fmt::format("stb_image failed to load '{}': {}", path.string(), stbi_failure_reason()),
                          ErrorCode::AssetParsingFailed);
    }

    const size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * desiredChannels;

    auto texture = std::make_shared<TextureData>();
    texture->name = path.stem().string();
    texture->sourcePath = path;
    texture->width = static_cast<std::uint32_t>(width);
    texture->height = static_cast<std::uint32_t>(height);
    texture->channels = static_cast<std::uint32_t>(desiredChannels);
    texture->pixels.assign(pixels, pixels + imageSize);

    stbi_image_free(pixels);

    return texture;
}
