#include "../Public/World.hpp"

#include <algorithm>

using namespace DirectX;

entt::entity World::create_entity(std::string name)
{
    entt::entity entity = m_registry.create();

    m_registry.emplace<NameComponent>(entity, std::move(name));
    m_registry.emplace<TransformComponent>(entity);
    m_registry.emplace<HierarchyComponent>(entity);
    m_registry.emplace<WorldMatrixCache>(entity);
    m_rootEntitiesDirty = true;

    return entity;
}

void World::destroy_entity(entt::entity entity)
{
    if (!m_registry.valid(entity))
        return;
    m_rootEntitiesDirty = true;

    // Recursively destroy all children first.
    if (auto* hierarchy = m_registry.try_get<HierarchyComponent>(entity))
    {
        // Copy children vector since we mutate it during recursion.
        auto children = hierarchy->children;
        for (entt::entity child : children)
        {
            destroy_entity(child);
        }

        // Remove ourselves from our parent's children list.
        if (hierarchy->parent != entt::null && m_registry.valid(hierarchy->parent))
        {
            if (auto* parentHierarchy = m_registry.try_get<HierarchyComponent>(hierarchy->parent))
            {
                auto& siblings = parentHierarchy->children;
                siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
            }
        }
    }

    m_registry.destroy(entity);
}

void World::set_parent(entt::entity child, entt::entity parent)
{
    if (!m_registry.valid(child) || !m_registry.valid(parent) || child == parent)
        return;
    m_rootEntitiesDirty = true;

    auto& childHierarchy = m_registry.get<HierarchyComponent>(child);

    // Remove from old parent if any.
    if (childHierarchy.parent != entt::null && m_registry.valid(childHierarchy.parent))
    {
        auto& oldParentHierarchy = m_registry.get<HierarchyComponent>(childHierarchy.parent);
        auto& siblings = oldParentHierarchy.children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    }

    // Attach to new parent.
    childHierarchy.parent = parent;
    auto& parentHierarchy = m_registry.get<HierarchyComponent>(parent);
    parentHierarchy.children.push_back(child);
}

void World::remove_parent(entt::entity child)
{
    if (!m_registry.valid(child))
        return;
    m_rootEntitiesDirty = true;

    auto& childHierarchy = m_registry.get<HierarchyComponent>(child);

    if (childHierarchy.parent == entt::null)
        return;

    if (m_registry.valid(childHierarchy.parent))
    {
        auto& parentHierarchy = m_registry.get<HierarchyComponent>(childHierarchy.parent);
        auto& siblings = parentHierarchy.children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    }

    childHierarchy.parent = entt::null;
}

void World::rebuild_root_entities_cache()
{
    m_rootEntitiesCache.clear();
    auto view = m_registry.view<HierarchyComponent>();
    for (auto entity : view)
    {
        const auto& hierarchy = view.get<HierarchyComponent>(entity);
        if (hierarchy.parent == entt::null)
        {
            m_rootEntitiesCache.push_back(entity);
        }
    }
    m_rootEntitiesDirty = false;
}

const std::vector<entt::entity>& World::get_root_entities()
{
    if (m_rootEntitiesDirty)
        rebuild_root_entities_cache();
    return m_rootEntitiesCache;
}

void World::update_world_matrices()
{
    // Start from all root entities and propagate downward.
    const auto& roots = get_root_entities();
    XMMATRIX identity = XMMatrixIdentity();
    for (entt::entity root : roots)
    {
        update_world_matrices_recursive(root, identity);
    }
}

void World::update_world_matrices_recursive(entt::entity entity, const XMMATRIX& parentWorld)
{
    const auto& transform = m_registry.get<TransformComponent>(entity);
    XMMATRIX localMatrix = transform.get_local_matrix();
    XMMATRIX worldMatrix = XMMatrixMultiply(localMatrix, parentWorld);

    auto& cache = m_registry.get<WorldMatrixCache>(entity);
    XMStoreFloat4x4(&cache.worldMatrix, worldMatrix);

    const auto& hierarchy = m_registry.get<HierarchyComponent>(entity);
    for (entt::entity child : hierarchy.children)
    {
        update_world_matrices_recursive(child, worldMatrix);
    }
}

const std::vector<Renderable>& World::collect_renderables()
{
    m_renderablesCache.clear();
    auto view = m_registry.view<MeshComponent, WorldMatrixCache>();
    m_renderablesCache.reserve(static_cast<size_t>(view.size_hint()));
    for (auto entity : view)
    {
        const auto& mesh = view.get<MeshComponent>(entity);
        if (mesh.meshIndex < 0)
            continue; // no valid mesh assigned

        const auto& cache = view.get<WorldMatrixCache>(entity);

        Renderable r{};
        r.worldMatrix = cache.worldMatrix;
        r.meshIndex = static_cast<uint32_t>(mesh.meshIndex);
        r.materialIndex = static_cast<uint32_t>(mesh.materialIndex);
        m_renderablesCache.push_back(r);
    }
    return m_renderablesCache;
}

entt::entity World::get_active_camera()
{
    auto view = m_registry.view<CameraComponent>();
    for (auto entity : view)
    {
        const auto& cam = view.get<CameraComponent>(entity);
        if (cam.isActive)
            return entity;
    }
    return entt::null;
}
