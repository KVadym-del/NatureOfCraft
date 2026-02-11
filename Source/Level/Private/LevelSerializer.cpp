#include "LevelSerializer.hpp"

#include <LevelAsset_generated.h>

#include <flatbuffers/flatbuffers.h>
#include <fmt/core.h>

#include <fstream>
#include <set>

namespace fb = flatbuffers;
namespace fbl = NatureOfCraft::Level;

// ── Serialize helpers ────────────────────────────────────────────────

/// Recursively serializes an entity and its children into the FlatBufferBuilder.
static fb::Offset<fbl::EntityData> serialize_entity(fb::FlatBufferBuilder& fbb, const World& world, entt::entity entity)
{
    auto& reg = const_cast<World&>(world).registry(); // we only read

    // Name
    const auto* nameComp = reg.try_get<NameComponent>(entity);
    fb::Offset<fb::String> nameOffset = nameComp ? fbb.CreateString(nameComp->name) : 0;

    // Transform
    fb::Offset<fbl::TransformData> transformOffset = 0;
    if (const auto* tc = reg.try_get<TransformComponent>(entity))
    {
        fbl::Vec3 pos(tc->position.x, tc->position.y, tc->position.z);
        fbl::Vec4 rot(tc->rotation.x, tc->rotation.y, tc->rotation.z, tc->rotation.w);
        fbl::Vec3 scl(tc->scale.x, tc->scale.y, tc->scale.z);
        transformOffset = fbl::CreateTransformData(fbb, &pos, &rot, &scl);
    }

    // Mesh component (optional)
    fb::Offset<fbl::MeshComponentData> meshOffset = 0;
    if (const auto* mc = reg.try_get<MeshComponent>(entity))
    {
        meshOffset = fbl::CreateMeshComponentDataDirect(fbb, mc->meshIndex, mc->materialIndex,
                                                        mc->assetPath.empty() ? nullptr : mc->assetPath.c_str());
    }

    // Camera component (optional)
    fb::Offset<fbl::CameraComponentData> cameraOffset = 0;
    if (const auto* cc = reg.try_get<CameraComponent>(entity))
    {
        fbl::Vec3 target(cc->target.x, cc->target.y, cc->target.z);
        cameraOffset = fbl::CreateCameraComponentData(fbb, cc->fov, cc->nearPlane, cc->farPlane, cc->isActive, &target,
                                                      cc->distance, cc->yaw, cc->pitch);
    }

    // Script component (optional)
    fb::Offset<fbl::ScriptComponentData> scriptOffset = 0;
    if (const auto* sc = reg.try_get<ScriptComponent>(entity))
    {
        if (!sc->scriptPath.empty())
            scriptOffset = fbl::CreateScriptComponentDataDirect(fbb, sc->scriptPath.c_str());
    }

    // Children (recursive)
    fb::Offset<fb::Vector<fb::Offset<fbl::EntityData>>> childrenOffset = 0;
    if (const auto* hc = reg.try_get<HierarchyComponent>(entity))
    {
        if (!hc->children.empty())
        {
            std::vector<fb::Offset<fbl::EntityData>> childOffsets;
            childOffsets.reserve(hc->children.size());
            for (entt::entity child : hc->children)
            {
                childOffsets.push_back(serialize_entity(fbb, world, child));
            }
            childrenOffset = fbb.CreateVector(childOffsets);
        }
    }

    return fbl::CreateEntityData(fbb, nameOffset, transformOffset, meshOffset, cameraOffset, scriptOffset,
                                 childrenOffset);
}

/// Collects all unique asset paths from MeshComponents and ScriptComponents for the referenced_assets list.
static std::vector<std::string> collect_referenced_assets(const World& world)
{
    auto& reg = const_cast<World&>(world).registry();
    std::set<std::string> assetPaths;

    for (auto [entity, mc] : reg.view<MeshComponent>().each())
    {
        if (!mc.assetPath.empty())
            assetPaths.insert(mc.assetPath);
    }

    for (auto [entity, sc] : reg.view<ScriptComponent>().each())
    {
        if (!sc.scriptPath.empty())
            assetPaths.insert(sc.scriptPath);
    }

    return {assetPaths.begin(), assetPaths.end()};
}

// ── Deserialize helpers ──────────────────────────────────────────────

/// Recursively deserializes an entity from FlatBuffers into the World.
/// Returns the created entity. If parentEntity != entt::null, sets the parent.
static entt::entity deserialize_entity(World& world, const fbl::EntityData* data, entt::entity parentEntity)
{
    std::string name = data->name() ? data->name()->str() : "";
    entt::entity entity = world.create_entity(std::move(name));

    // Transform
    if (const auto* td = data->transform())
    {
        auto& tc = world.registry().get<TransformComponent>(entity);
        if (td->position())
            tc.position = {td->position()->x(), td->position()->y(), td->position()->z()};
        if (td->rotation())
            tc.rotation = {td->rotation()->x(), td->rotation()->y(), td->rotation()->z(), td->rotation()->w()};
        if (td->scale())
            tc.scale = {td->scale()->x(), td->scale()->y(), td->scale()->z()};
    }

    // Mesh component
    if (const auto* md = data->mesh())
    {
        auto& mc = world.registry().emplace<MeshComponent>(entity);
        mc.meshIndex = md->mesh_index();
        mc.materialIndex = md->material_index();
        mc.assetPath = md->asset_path() ? md->asset_path()->str() : "";
    }

    // Camera component
    if (const auto* cd = data->camera())
    {
        auto& cc = world.registry().emplace<CameraComponent>(entity);
        cc.fov = cd->fov();
        cc.nearPlane = cd->near_plane();
        cc.farPlane = cd->far_plane();
        cc.isActive = cd->is_active();
        if (cd->target())
            cc.target = {cd->target()->x(), cd->target()->y(), cd->target()->z()};
        cc.distance = cd->distance();
        cc.yaw = cd->yaw();
        cc.pitch = cd->pitch();
    }

    // Script component
    if (const auto* sd = data->script())
    {
        auto& sc = world.registry().emplace<ScriptComponent>(entity);
        sc.scriptPath = sd->script_path() ? sd->script_path()->str() : "";
    }

    // Set parent
    if (parentEntity != entt::null)
        world.set_parent(entity, parentEntity);

    // Children
    if (const auto* children = data->children())
    {
        for (const auto* childData : *children)
        {
            deserialize_entity(world, childData, entity);
        }
    }

    return entity;
}

// ── Public API ───────────────────────────────────────────────────────

Result<std::vector<uint8_t>> LevelSerializer::serialize(const World& world, const std::string& levelName)
{
    fb::FlatBufferBuilder fbb(4096);

    // Serialize root entities
    auto& mutableWorld = const_cast<World&>(world);
    auto roots = mutableWorld.get_root_entities();

    std::vector<fb::Offset<fbl::EntityData>> rootOffsets;
    rootOffsets.reserve(roots.size());
    for (entt::entity root : roots)
    {
        rootOffsets.push_back(serialize_entity(fbb, world, root));
    }

    // Collect referenced assets
    auto assetPaths = collect_referenced_assets(world);
    std::vector<fb::Offset<fb::String>> assetOffsets;
    assetOffsets.reserve(assetPaths.size());
    for (const auto& path : assetPaths)
    {
        assetOffsets.push_back(fbb.CreateString(path));
    }

    auto nameOffset = fbb.CreateString(levelName);
    auto rootEntitiesOffset = fbb.CreateVector(rootOffsets);
    auto referencedAssetsOffset = fbb.CreateVector(assetOffsets);

    auto level = fbl::CreateLevelAsset(fbb, nameOffset, 1, rootEntitiesOffset, referencedAssetsOffset);
    fbl::FinishLevelAssetBuffer(fbb, level);

    const uint8_t* buf = fbb.GetBufferPointer();
    size_t size = fbb.GetSize();
    return std::vector<uint8_t>(buf, buf + size);
}

Result<> LevelSerializer::deserialize(const std::vector<uint8_t>& buffer, World& world)
{
    // Verify buffer
    fb::Verifier verifier(buffer.data(), buffer.size());
    if (!fbl::VerifyLevelAssetBuffer(verifier))
    {
        return make_error("Invalid or corrupted level file", ErrorCode::AssetParsingFailed);
    }

    const auto* level = fbl::GetLevelAsset(buffer.data());
    if (!level)
    {
        return make_error("Failed to parse level asset", ErrorCode::AssetParsingFailed);
    }

    // Deserialize root entities
    if (const auto* rootEntities = level->root_entities())
    {
        for (const auto* entityData : *rootEntities)
        {
            deserialize_entity(world, entityData, entt::null);
        }
    }

    return {};
}

Result<> LevelSerializer::save_to_file(const World& world, const std::string& levelName, const std::string& filePath)
{
    auto bufferResult = serialize(world, levelName);
    if (!bufferResult)
        return make_error(bufferResult.error());

    std::ofstream file(filePath, std::ios::binary);
    if (!file)
        return make_error(fmt::format("Failed to open file for writing: {}", filePath),
                          ErrorCode::AssetCacheWriteFailed);

    const auto& buffer = bufferResult.value();
    file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (!file)
        return make_error(fmt::format("Failed to write level to file: {}", filePath), ErrorCode::AssetCacheWriteFailed);

    return {};
}

Result<> LevelSerializer::load_from_file(const std::string& filePath, World& world)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file)
        return make_error(fmt::format("Failed to open level file: {}", filePath), ErrorCode::AssetFileNotFound);

    auto size = file.tellg();
    if (size <= 0)
        return make_error(fmt::format("Level file is empty: {}", filePath), ErrorCode::AssetParsingFailed);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    if (!file)
        return make_error(fmt::format("Failed to read level file: {}", filePath), ErrorCode::AssetCacheReadFailed);

    return deserialize(buffer, world);
}
