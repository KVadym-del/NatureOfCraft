#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Rendering/Public/Renderable.hpp"
#include "Components.hpp"

#include <string>
#include <vector>

#include <entt/entity/registry.hpp>

NOC_SUPPRESS_DLL_WARNINGS

/// ECS world: wraps an entt::registry and provides entity lifecycle management,
/// parent-child hierarchy, world matrix propagation, and renderable collection.
class NOC_EXPORT World
{
  public:
    World() = default;

    // ── Entity lifecycle ──────────────────────────────────────────────

    /// Creates a new entity with Name, Transform, Hierarchy, and WorldMatrixCache.
    entt::entity create_entity(std::string name = "");

    /// Recursively destroys an entity and all its descendants.
    /// Removes itself from its parent's children list before destruction.
    void destroy_entity(entt::entity entity);

    // ── Hierarchy management ──────────────────────────────────────────

    /// Sets `parent` as the parent of `child`.
    /// If `child` already has a parent, it is removed from the old parent first.
    void set_parent(entt::entity child, entt::entity parent);

    /// Removes `child` from its parent, making it a root entity.
    void remove_parent(entt::entity child);

    /// Returns all root-level entities (those with parent == entt::null).
    const std::vector<entt::entity>& get_root_entities();

    // ── Systems (per-frame) ───────────────────────────────────────────

    /// Recomputes WorldMatrixCache for every entity from root to leaf.
    void update_world_matrices();

    /// Builds a flat list of Renderables from all entities with MeshComponent + WorldMatrixCache.
    const std::vector<Renderable>& collect_renderables();

    /// Returns the first entity whose CameraComponent has isActive == true, or entt::null.
    entt::entity get_active_camera();

    // ── Registry access ───────────────────────────────────────────────

    entt::registry& registry() noexcept
    {
        return m_registry;
    }
    const entt::registry& registry() const noexcept
    {
        return m_registry;
    }

  private:
    void rebuild_root_entities_cache();
    void update_world_matrices_recursive(entt::entity entity, const DirectX::XMMATRIX& parentWorld);

    entt::registry m_registry;
    std::vector<entt::entity> m_rootEntitiesCache;
    bool m_rootEntitiesDirty{true};
    std::vector<Renderable> m_renderablesCache;
};

NOC_RESTORE_DLL_WARNINGS
