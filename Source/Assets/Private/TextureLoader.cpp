#include "TextureLoader.hpp"

#include <fmt/core.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>

TextureLoader::result_type TextureLoader::operator()(std::string_view path) const
{
    auto result = load_image(path);
    if (!result)
    {
        fmt::print(stderr, "TextureLoader: failed to load '{}': {}\n", path, result.error().message);
        return nullptr;
    }
    return std::move(result.value());
}

TextureLoader::result_type TextureLoader::operator()(const TextureData& data) const
{
    return std::make_shared<TextureData>(data);
}

Result<std::shared_ptr<TextureData>> TextureLoader::load_image(std::string_view path)
{
    namespace fs = std::filesystem;

    const std::string pathStr(path);
    if (!fs::exists(pathStr))
    {
        return make_error(fmt::format("Texture file not found: {}", path), ErrorCode::AssetFileNotFound);
    }

    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    constexpr int desiredChannels = 4; // Force RGBA

    stbi_uc* pixels = stbi_load(pathStr.c_str(), &width, &height, &channelsInFile, desiredChannels);
    if (!pixels)
    {
        return make_error(fmt::format("stb_image failed to load '{}': {}", path, stbi_failure_reason()),
                          ErrorCode::AssetParsingFailed);
    }

    const size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * desiredChannels;

    auto texture = std::make_shared<TextureData>();
    texture->name = fs::path(pathStr).stem().string();
    texture->sourcePath = pathStr;
    texture->width = static_cast<uint32_t>(width);
    texture->height = static_cast<uint32_t>(height);
    texture->channels = desiredChannels;
    texture->pixels.assign(pixels, pixels + imageSize);

    stbi_image_free(pixels);

    return texture;
}
