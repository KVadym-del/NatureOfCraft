#pragma once
#include "../../Core/Public/Expected.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

class Scene;
class SceneNode;
class AssetManager;
class IRenderer;

/// Serializes / deserializes a Scene to/from a FlatBuffer binary file (.noc_scene).
///
/// Mesh references are stored by name (file path) in the FlatBuffer.
/// On save, a runtime mesh-index -> path mapping must be provided.
/// On load, meshes are loaded via AssetManager and uploaded to the renderer,
/// producing runtime mesh indices for each SceneNode.
struct SceneSerializer
{
    /// Mapping from runtime mesh index to asset path (used when saving).
    using MeshIndexToPath = std::unordered_map<int32_t, std::string>;

    /// Save a scene to a binary file.
    /// @param scene        The scene graph to serialize.
    /// @param filePath     Output file path (e.g. "Resources/scene.noc_scene").
    /// @param meshPaths    Maps each mesh index to its source asset path.
    static Result<> save(const Scene& scene, std::string_view filePath, const MeshIndexToPath& meshPaths);

    /// Load a scene from a binary file.
    /// Meshes referenced in the file are loaded via AssetManager and uploaded
    /// to the renderer. The resulting mesh indices are assigned to SceneNodes.
    /// @param filePath     Input file path.
    /// @param assets       AssetManager used to load mesh data.
    /// @param renderer     Renderer used to upload meshes to GPU.
    /// @return             The deserialized Scene.
    static Result<Scene> load(std::string_view filePath, AssetManager& assets, IRenderer& renderer);
};
