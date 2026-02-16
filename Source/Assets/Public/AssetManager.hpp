#pragma once
#include "../../Core/Public/Core.hpp"
#include "../Private/MeshLoader.hpp"
#include "../Private/ModelLoader.hpp"
#include "../Private/TextureLoader.hpp"
#include "MaterialData.hpp"
#include "MeshData.hpp"
#include "ModelData.hpp"
#include "TextureData.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>
#include <filesystem>

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
    /// Constructs an empty asset manager with lazy async executor initialization.
    AssetManager();
    /// Waits for queued async work (if any) and releases all owned caches.
    ~AssetManager();

    // Non-copyable, non-movable (owns executor + caches)
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // --- Mesh loading ---

    /// Load a mesh synchronously. Returns a resource handle.
    /// Uses FlatBuffer binary cache if available, otherwise parses OBJ.
    /// Deduplicates: loading the same path twice returns the same handle.
    entt::resource<MeshData> load_mesh(const std::filesystem::path& path);

    /// Check if a mesh with the given path is already loaded.
    bool contains_mesh(const std::filesystem::path& path) const;

    /// Get a mesh by path if already loaded. Returns an invalid handle if not found.
    entt::resource<const MeshData> get_mesh(const std::filesystem::path& path) const;

    /// Number of loaded meshes.
    size_t mesh_count() const noexcept;

    /// Remove all cached meshes.
    void clear_meshes() noexcept;

    // --- Material loading (data model ready, minimal implementation) ---

    /// Inserts or replaces a material entry under the given logical name.
    entt::resource<MaterialData> load_material(std::string_view name, const MaterialData& data);

    /// Returns true if a material handle for the given name is cached.
    bool contains_material(std::string_view name) const;
    /// Returns the number of cached materials.
    size_t material_count() const noexcept;
    /// Clears all cached material handles.
    void clear_materials() noexcept;

    // --- Texture loading ---

    /// Load a texture from an image file (PNG, JPG, TGA, etc.).
    /// Forces RGBA8 output. Deduplicates by path.
    entt::resource<TextureData> load_texture(const std::filesystem::path& path);

    /// Load a texture from pre-built data (e.g. programmatic textures).
    entt::resource<TextureData> load_texture(std::string_view name, const TextureData& data);

    /// Returns true if a texture handle with this key is cached.
    bool contains_texture(std::string_view name) const;
    /// Returns the number of cached textures.
    size_t texture_count() const noexcept;
    /// Clears all cached texture handles.
    void clear_textures() noexcept;

    // --- Model loading (OBJ + MTL, splits by material) ---

    /// Load a complete model (OBJ + MTL). Splits geometry by material,
    /// extracts texture paths, computes tangents per sub-mesh.
    entt::resource<ModelData> load_model(const std::filesystem::path& path);

    /// Returns true if a model handle for this path is cached.
    bool contains_model(const std::filesystem::path& path) const;
    /// Returns the number of cached models.
    size_t model_count() const noexcept;
    /// Clears all cached model handles.
    void clear_models() noexcept;

    // --- Async support ---

    /// Returns a reference to the Taskflow executor for scheduling async work.
    tf::Executor& get_executor() noexcept
    {
        if (!m_executor)
        {
            const std::uint32_t workers = std::max(1u, std::thread::hardware_concurrency());
            m_executor = std::make_unique<tf::Executor>(workers);
        }
        return *m_executor;
    }

  private:
    /// Compute a stable ID from a file path for use as entt::resource_cache key.
    static entt::id_type path_to_id(const std::filesystem::path& path);

    entt::resource_cache<MeshData, MeshLoader> m_meshCache;
    entt::resource_cache<MaterialData> m_materialCache;
    entt::resource_cache<TextureData, TextureLoader> m_textureCache;
    entt::resource_cache<ModelData, ModelLoader> m_modelCache;

    std::unique_ptr<tf::Executor> m_executor;
};

NOC_RESTORE_DLL_WARNINGS
