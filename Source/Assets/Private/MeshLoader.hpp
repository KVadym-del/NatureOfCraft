#pragma once
#include "../../Core/Public/Expected.hpp"
#include "../Public/MeshData.hpp"

#include <filesystem>
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
    result_type operator()(const std::filesystem::path& path) const;

    /// Load from either OBJ or FBX, returning the parsed MeshData (no caching).
    static Result<std::shared_ptr<MeshData>> parse_mesh(const std::filesystem::path& path);

    /// Parse an OBJ file directly into MeshData (no caching).
    static Result<std::shared_ptr<MeshData>> parse_obj(const std::filesystem::path& path);

    /// Parse an FBX file directly into MeshData (no caching).
    static Result<std::shared_ptr<MeshData>> parse_fbx(const std::filesystem::path& path);

    /// Serialize MeshData to a FlatBuffer binary cache file.
    static Result<> write_cache(const MeshData& mesh, const std::filesystem::path& cachePath);

    /// Deserialize MeshData from a FlatBuffer binary cache file.
    static Result<std::shared_ptr<MeshData>> read_cache(const std::filesystem::path& cachePath);

    /// Returns the cache file path for a given source path.
    /// e.g. "Resources/Mustang.obj" -> "Resources/Mustang.noc_mesh"
    static std::filesystem::path get_cache_path(std::filesystem::path& sourcePath);
};
