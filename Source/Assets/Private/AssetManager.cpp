#include "../Public/AssetManager.hpp"
#include "MeshLoader.hpp"
#include "ModelLoader.hpp"
#include "TextureLoader.hpp"

#include <entt/core/hashed_string.hpp>

AssetManager::AssetManager()
    : m_meshCache(MeshLoader{}), m_textureCache(TextureLoader{}), m_modelCache(ModelLoader{})
{}

AssetManager::~AssetManager()
{
    if (m_executor)
        m_executor->wait_for_all();
}

entt::id_type AssetManager::path_to_id(const std::filesystem::path& path)
{
    return entt::hashed_string{path.string().data(), path.string().size()};
}

// --- Mesh ---

entt::resource<MeshData> AssetManager::load_mesh(const std::filesystem::path& path)
{
    auto id = path_to_id(path);
    auto [it, inserted] = m_meshCache.load(id, path);
    return it->second;
}

bool AssetManager::contains_mesh(const std::filesystem::path& path) const
{
    return m_meshCache.contains(path_to_id(path));
}

entt::resource<const MeshData> AssetManager::get_mesh(const std::filesystem::path& path) const
{
    return m_meshCache[path_to_id(path)];
}

size_t AssetManager::mesh_count() const noexcept
{
    return m_meshCache.size();
}

void AssetManager::clear_meshes() noexcept
{
    m_meshCache.clear();
}

// --- Material (stub implementation — data model ready, GPU upload deferred) ---

entt::resource<MaterialData> AssetManager::load_material(std::string_view name, const MaterialData& data)
{
    auto id = path_to_id(name);
    auto [it, inserted] = m_materialCache.load(id, data);
    return it->second;
}

bool AssetManager::contains_material(std::string_view name) const
{
    return m_materialCache.contains(path_to_id(name));
}

size_t AssetManager::material_count() const noexcept
{
    return m_materialCache.size();
}

void AssetManager::clear_materials() noexcept
{
    m_materialCache.clear();
}

// --- Texture (stub implementation — data model ready, GPU upload deferred) ---

entt::resource<TextureData> AssetManager::load_texture(const std::filesystem::path& path)
{
    auto id = path_to_id(path);
    auto [it, inserted] = m_textureCache.load(id, path);
    return it->second;
}

entt::resource<TextureData> AssetManager::load_texture(std::string_view name, const TextureData& data)
{
    auto id = path_to_id(name);
    auto [it, inserted] = m_textureCache.load(id, data);
    return it->second;
}

bool AssetManager::contains_texture(std::string_view name) const
{
    return m_textureCache.contains(path_to_id(name));
}

size_t AssetManager::texture_count() const noexcept
{
    return m_textureCache.size();
}

void AssetManager::clear_textures() noexcept
{
    m_textureCache.clear();
}

// --- Model ---

entt::resource<ModelData> AssetManager::load_model(const std::filesystem::path& path)
{
    auto id = path_to_id(path);
    auto [it, inserted] = m_modelCache.load(id, path);
    return it->second;
}

bool AssetManager::contains_model(const std::filesystem::path& path) const
{
    return m_modelCache.contains(path_to_id(path));
}

size_t AssetManager::model_count() const noexcept
{
    return m_modelCache.size();
}

void AssetManager::clear_models() noexcept
{
    m_modelCache.clear();
}
