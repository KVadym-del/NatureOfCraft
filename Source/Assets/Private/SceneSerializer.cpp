#include "SceneSerializer.hpp"
#include "../../Core/Public/Utils.hpp"
#include "../../Rendering/Public/IRenderer.hpp"
#include "../../Scene/Public/Scene.hpp"
#include "../Public/AssetManager.hpp"

#include <SceneAsset_generated.h>

#include <filesystem>
#include <fstream>

namespace fbs = NatureOfCraft::Assets;

// ──────────────────────────────────────────────────────────────
// Saving
// ──────────────────────────────────────────────────────────────

/// Recursively serialize a SceneNode subtree into FlatBuffer offsets.
static flatbuffers::Offset<fbs::SceneNodeData> serialize_node(flatbuffers::FlatBufferBuilder& builder,
                                                              const SceneNode* node,
                                                              const SceneSerializer::MeshIndexToPath& meshPaths)
{
    // Transform
    const auto& t = node->get_transform();
    fbs::Vec3 pos(t.position.x, t.position.y, t.position.z);
    fbs::Vec4 rot(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
    fbs::Vec3 scl(t.scale.x, t.scale.y, t.scale.z);
    auto transformOffset = fbs::CreateTransformData(builder, &pos, &rot, &scl);

    // Name
    auto nameOffset = builder.CreateString(node->get_name());

    // Mesh path (empty string if no mesh)
    flatbuffers::Offset<flatbuffers::String> meshNameOffset{};
    if (node->has_mesh())
    {
        auto it = meshPaths.find(node->get_mesh_index());
        if (it != meshPaths.end())
            meshNameOffset = builder.CreateString(it->second);
    }

    // Children (recurse)
    std::vector<flatbuffers::Offset<fbs::SceneNodeData>> childOffsets;
    for (const auto& child : node->get_children())
    {
        childOffsets.push_back(serialize_node(builder, child.get(), meshPaths));
    }
    auto childrenOffset = builder.CreateVector(childOffsets);

    return fbs::CreateSceneNodeData(builder, nameOffset, transformOffset, meshNameOffset, 0, childrenOffset);
}

Result<> SceneSerializer::save(const Scene& scene, std::string_view filePath, const MeshIndexToPath& meshPaths)
{
    flatbuffers::FlatBufferBuilder builder(1024);

    // Serialize root's children (the root node itself is implicit)
    const SceneNode* root = scene.get_root();
    std::vector<flatbuffers::Offset<fbs::SceneNodeData>> rootNodeOffsets;
    for (const auto& child : root->get_children())
    {
        rootNodeOffsets.push_back(serialize_node(builder, child.get(), meshPaths));
    }

    auto nameOffset = builder.CreateString("scene");
    auto rootNodesOffset = builder.CreateVector(rootNodeOffsets);
    auto sceneAsset = fbs::CreateSceneAsset(builder, nameOffset, rootNodesOffset);
    fbs::FinishSceneAssetBuffer(builder, sceneAsset);

    std::ofstream out(std::filesystem::path{std::string{filePath}}, std::ios::binary);
    if (!out)
        return make_error("Failed to open scene file for writing", ErrorCode::AssetCacheWriteFailed);

    out.write(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize());
    if (!out)
        return make_error("Failed to write scene file", ErrorCode::AssetCacheWriteFailed);

    return {};
}

// ──────────────────────────────────────────────────────────────
// Loading
// ──────────────────────────────────────────────────────────────

/// Recursively deserialize a SceneNodeData into SceneNode children.
/// Loads mesh assets and uploads them to the renderer on demand.
/// @param meshPathToIndex  Caches mesh path -> GPU mesh index mapping to avoid duplicate uploads.
static Result<> deserialize_node(const fbs::SceneNodeData* fbsNode, SceneNode* parentNode, AssetManager& assets,
                                 IRenderer& renderer, std::unordered_map<std::string, int32_t>& meshPathToIndex)
{
    if (!fbsNode)
        return {};

    std::string name = fbsNode->name() ? fbsNode->name()->str() : "";
    SceneNode* node = parentNode->add_child(std::move(name));

    // Restore transform
    if (auto* t = fbsNode->transform())
    {
        auto& transform = node->get_transform();
        if (auto* p = t->position())
            transform.position = {p->x(), p->y(), p->z()};
        if (auto* r = t->rotation())
            transform.rotation = {r->x(), r->y(), r->z(), r->w()};
        if (auto* s = t->scale())
            transform.scale = {s->x(), s->y(), s->z()};
    }

    // Restore mesh reference
    if (fbsNode->mesh_name() && fbsNode->mesh_name()->size() > 0)
    {
        std::string meshPath = fbsNode->mesh_name()->str();

        // Check if already uploaded
        auto it = meshPathToIndex.find(meshPath);
        if (it != meshPathToIndex.end())
        {
            node->set_mesh_index(it->second);
        }
        else
        {
            // Load via AssetManager (CPU-side, with FlatBuffer caching)
            auto meshHandle = assets.load_mesh(meshPath);
            // Upload to GPU
            auto uploadResult = renderer.upload_mesh(*meshHandle);
            if (!uploadResult)
                return make_error(uploadResult.error());

            int32_t meshIndex = static_cast<int32_t>(uploadResult.value());
            meshPathToIndex[meshPath] = meshIndex;
            node->set_mesh_index(meshIndex);
        }
    }

    // Recurse children
    if (auto* children = fbsNode->children())
    {
        for (auto* child : *children)
        {
            auto result = deserialize_node(child, node, assets, renderer, meshPathToIndex);
            if (!result)
                return result;
        }
    }

    return {};
}

Result<Scene> SceneSerializer::load(std::string_view filePath, AssetManager& assets, IRenderer& renderer)
{
    auto fileResult = read_file(filePath);
    if (!fileResult)
        return make_error(fileResult.error());

    const auto& fileData = fileResult.value();
    flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(fileData.data()), fileData.size());
    if (!fbs::VerifySceneAssetBuffer(verifier))
        return make_error("Invalid scene file", ErrorCode::AssetInvalidData);

    const auto* sceneAsset = fbs::GetSceneAsset(fileData.data());
    if (!sceneAsset)
        return make_error("Failed to parse scene file", ErrorCode::AssetParsingFailed);

    Scene scene;
    std::unordered_map<std::string, int32_t> meshPathToIndex;

    if (auto* rootNodes = sceneAsset->root_nodes())
    {
        for (auto* fbsNode : *rootNodes)
        {
            auto result = deserialize_node(fbsNode, scene.get_root(), assets, renderer, meshPathToIndex);
            if (!result)
                return make_error(result.error());
        }
    }

    return scene;
}
