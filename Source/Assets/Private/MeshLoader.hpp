#pragma once
#include "../../Core/Public/Expected.hpp"
#include "../Public/MeshData.hpp"

#include <memory>
#include <string_view>

/// Loads mesh data from OBJ files, with FlatBuffer binary caching.
/// Conforms to the EnTT resource_cache loader concept:
///   operator()(args...) -> shared_ptr<MeshData>
///
/// Usage with entt::resource_cache:
///   entt::resource_cache<MeshData, MeshLoader> cache;
///   cache.load(id, "path/to/model.obj");
struct MeshLoader
{
    using result_type = std::shared_ptr<MeshData>;

    /// Load a mesh from the given file path.
    /// Checks for a binary cache file (.noc_mesh) first.
    /// Falls back to OBJ parsing, then writes the cache.
    result_type operator()(std::string_view path) const;

    /// Parse an OBJ file directly into MeshData (no caching).
    static Result<std::shared_ptr<MeshData>> parse_obj(std::string_view path);

    /// Serialize MeshData to a FlatBuffer binary cache file.
    static Result<> write_cache(const MeshData& mesh, std::string_view cachePath);

    /// Deserialize MeshData from a FlatBuffer binary cache file.
    static Result<std::shared_ptr<MeshData>> read_cache(std::string_view cachePath);

    /// Returns the cache file path for a given source path.
    /// e.g. "Resources/Mustang.obj" -> "Resources/Mustang.noc_mesh"
    static std::string get_cache_path(std::string_view sourcePath);
};
