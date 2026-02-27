#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Core/Public/Expected.hpp"

#include <entt/entity/entity.hpp>

#include <cstdint>
#include <memory>

class World;

NOC_SUPPRESS_DLL_WARNINGS

class NOC_EXPORT PhysicsWorld
{
  public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    ::Result<> initialize();
    void shutdown();

    void set_enabled(bool enabled) noexcept;
    bool is_enabled() const noexcept;

    void step(World& world, float deltaTime);
    void clear();

    void on_entity_tree_destroyed(World& world, entt::entity entity);
    void on_entity_removed(entt::entity entity);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

NOC_RESTORE_DLL_WARNINGS
