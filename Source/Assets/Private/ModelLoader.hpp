#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Core/Public/Expected.hpp"
#include "../Public/ModelData.hpp"

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
    result_type operator()(std::string_view path) const;

    /// Load with full error reporting (OBJ + MTL parsing).
    static Result<std::shared_ptr<ModelData>> parse_model(std::string_view path);

    /// Serialize ModelData to a FlatBuffer binary cache file (.noc_model).
    /// Texture paths stored in materials should already be project-relative.
    static Result<> write_cache(const ModelData& model, std::string_view cachePath);

    /// Deserialize ModelData from a FlatBuffer binary cache file (.noc_model).
    static Result<std::shared_ptr<ModelData>> read_cache(std::string_view cachePath);

    /// Returns the cache file path for a given source path.
    /// e.g. "Models/tower.obj" -> "Models/tower.noc_model"
    static std::string get_cache_path(std::string_view sourcePath);
};
