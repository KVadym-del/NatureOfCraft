#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Core/Public/Expected.hpp"
#include "../Public/ModelData.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

/// Loads a complete model (OBJ + MTL) into ModelData.
/// Splits geometry by material, extracts material properties and texture paths,
/// computes tangent vectors per sub-mesh.
///
/// Supports FlatBuffers binary caching (.noc_model) for self-contained projects:
///   write_cache() serializes ModelData to a binary file
///   read_cache() deserializes ModelData from a binary file
///
/// Conforms to the EnTT resource_cache loader concept:
///   operator()(args...) -> shared_ptr<ModelData>
struct NOC_EXPORT ModelLoader
{
    using result_type = std::shared_ptr<ModelData>;

    /// Load a model from the given file path.
    /// If the path ends with .noc_model, loads from cache directly.
    /// Otherwise parses OBJ + MTL.
    /// Returns nullptr on failure (EnTT cache convention).
    result_type operator()(const std::filesystem::path& path) const;

    /// Load with full error reporting (dispatches to parse_obj or parse_fbx).
    static Result<std::shared_ptr<ModelData>> parse_model(const std::filesystem::path& path);

    /// Parse an OBJ + MTL file directly.
    static Result<std::shared_ptr<ModelData>> parse_obj(const std::filesystem::path& path);

    /// Parse an FBX file directly.
    static Result<std::shared_ptr<ModelData>> parse_fbx(const std::filesystem::path& path);

    /// Serialize ModelData to a FlatBuffer binary cache file (.noc_model).
    /// Texture paths stored in materials should already be project-relative.
    static Result<> write_cache(const ModelData& model, const std::filesystem::path& cachePath);

    /// Deserialize ModelData from a FlatBuffer binary cache file (.noc_model).
    static Result<std::shared_ptr<ModelData>> read_cache(const std::filesystem::path& cachePath);

    /// Returns the cache file path for a given source path.
    /// e.g. "Models/tower.obj" -> "Models/tower.noc_model"
    static std::filesystem::path get_cache_path(std::filesystem::path& sourcePath);
};
