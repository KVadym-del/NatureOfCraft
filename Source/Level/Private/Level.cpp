#include "../Public/Level.hpp"
#include "LevelSerializer.hpp"

#include <fmt/core.h>

// ── Factories ────────────────────────────────────────────────────────

Level Level::create_new(std::string name)
{
    Level level;
    level.m_name = std::move(name);
    level.m_dirty = true;

    // Create a default active camera entity so the level is immediately usable.
    entt::entity cameraEntity = level.m_world.create_entity("Camera");
    auto& cam = level.m_world.registry().emplace<CameraComponent>(cameraEntity);
    cam.isActive = true;
    cam.fov = 45.0f;
    cam.nearPlane = 0.1f;
    cam.farPlane = 1000.0f;
    cam.target = {0.0f, 1.0f, 0.0f};
    cam.distance = 10.0f;
    cam.yaw = 0.0f;
    cam.pitch = 0.0f;

    return level;
}

Result<Level> Level::load(const std::string& filePath)
{
    Level level;

    auto result = LevelSerializer::load_from_file(filePath, level.m_world);
    if (!result)
        return make_error(result.error());

    level.m_filePath = filePath;
    level.m_dirty = false;

    // Extract level name from the file (the FlatBuffer stores it), but as a fallback
    // derive it from the file path.
    // For now, use the file stem as the name since LevelSerializer::load_from_file
    // doesn't return the name field separately.
    auto lastSlash = filePath.find_last_of("/\\");
    auto lastDot = filePath.rfind('.');
    if (lastDot != std::string::npos && (lastSlash == std::string::npos || lastDot > lastSlash))
        level.m_name = filePath.substr(lastSlash == std::string::npos ? 0 : lastSlash + 1,
                                       lastDot - (lastSlash == std::string::npos ? 0 : lastSlash + 1));
    else
        level.m_name = filePath.substr(lastSlash == std::string::npos ? 0 : lastSlash + 1);

    return level;
}

// ── Persistence ──────────────────────────────────────────────────────

Result<> Level::save()
{
    if (m_filePath.empty())
        return make_error("Cannot save: no file path set. Use save_as() first.", ErrorCode::AssetCacheWriteFailed);

    auto result = LevelSerializer::save_to_file(m_world, m_name, m_filePath);
    if (!result)
        return make_error(result.error());

    m_dirty = false;
    return {};
}

Result<> Level::save_as(const std::string& filePath)
{
    auto result = LevelSerializer::save_to_file(m_world, m_name, filePath);
    if (!result)
        return make_error(result.error());

    m_filePath = filePath;
    m_dirty = false;
    return {};
}
