#pragma once
#include "../../Core/Public/Core.hpp"
#include "../Private/MeshLoader.hpp"
#include "MaterialData.hpp"
#include "MeshData.hpp"
#include "TextureData.hpp"

#include <string_view>

#include <entt/core/hashed_string.hpp>
#include <entt/resource/cache.hpp>
#include <entt/resource/resource.hpp>
#include <taskflow/taskflow.hpp>

NOC_SUPPRESS_DLL_WARNINGS

/// Manages CPU-side asset data with deduplication, caching, and async loading.
///
/// Uses entt::resource_cache for handle-based lifecycle management:
///   - Same path -> same handle (automatic deduplication via hashed path ID)
///   - Shared ownership via entt::resource<T> (wraps shared_ptr)
///
/// Uses Taskflow for background CPU-side loading (OBJ parsing, FlatBuffer deserialization).
/// GPU upload is NOT handled here — the renderer owns GPU resources.
class NOC_EXPORT AssetManager
{
  public:
    AssetManager();
    ~AssetManager();

    // Non-copyable, non-movable (owns executor + caches)
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // --- Mesh loading ---

    /// Load a mesh synchronously. Returns a resource handle.
    /// Uses FlatBuffer binary cache if available, otherwise parses OBJ.
    /// Deduplicates: loading the same path twice returns the same handle.
    entt::resource<MeshData> load_mesh(std::string_view path);

    /// Check if a mesh with the given path is already loaded.
    bool contains_mesh(std::string_view path) const;

    /// Get a mesh by path if already loaded. Returns an invalid handle if not found.
    entt::resource<const MeshData> get_mesh(std::string_view path) const;

    /// Number of loaded meshes.
    size_t mesh_count() const noexcept;

    /// Remove all cached meshes.
    void clear_meshes() noexcept;

    // --- Material loading (data model ready, minimal implementation) ---

    entt::resource<MaterialData> load_material(std::string_view name, const MaterialData& data);

    bool contains_material(std::string_view name) const;
    size_t material_count() const noexcept;
    void clear_materials() noexcept;

    // --- Texture loading (data model ready, minimal implementation) ---

    entt::resource<TextureData> load_texture(std::string_view name, const TextureData& data);

    bool contains_texture(std::string_view name) const;
    size_t texture_count() const noexcept;
    void clear_textures() noexcept;

    // --- Async support ---

    /// Returns a reference to the Taskflow executor for scheduling async work.
    tf::Executor& get_executor() noexcept
    {
        return m_executor;
    }

  private:
    /// Compute a stable ID from a file path for use as entt::resource_cache key.
    static entt::id_type path_to_id(std::string_view path);

    entt::resource_cache<MeshData, MeshLoader> m_meshCache;
    entt::resource_cache<MaterialData> m_materialCache;
    entt::resource_cache<TextureData> m_textureCache;

    tf::Executor m_executor;
};

NOC_RESTORE_DLL_WARNINGS
