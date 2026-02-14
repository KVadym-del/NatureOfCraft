#pragma once

#include "../../Core/Public/Core.hpp"
#include "../../Core/Public/Expected.hpp"

#include <entt/entity/entity.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>

class World;

NOC_SUPPRESS_DLL_WARNINGS

/// Manages the LuaJIT VM and per-entity script lifecycle.
///
/// Each entity with a ScriptComponent gets a sandboxed sol::environment.
/// Scripts define optional lifecycle functions: on_start(entity), on_update(entity, dt), on_destroy(entity).
class NOC_EXPORT ScriptEngine
{
  public:
    ScriptEngine();
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;
    ScriptEngine(ScriptEngine&&) noexcept;
    ScriptEngine& operator=(ScriptEngine&&) noexcept;

    /// Initialize the Lua VM and register all C++ type bindings.
    Result<> initialize();

    /// Set the base directory for resolving script paths.
    /// Typically the project root or the executable directory.
    void set_script_root(const std::filesystem::path& root);

    /// Load & attach a script to an entity. Creates a sandboxed environment.
    Result<> load_script(World& world, entt::entity entity);

    /// Called every frame. Runs on_start (once) and on_update for all ScriptComponent entities.
    void update(World& world, float dt);

    /// Called when an entity with ScriptComponent is about to be destroyed.
    /// Invokes on_destroy and cleans up the Lua environment.
    void on_entity_destroyed(World& world, entt::entity entity);

    /// Called when an entity tree (entity + descendants) is about to be destroyed.
    /// Ensures all script environments in the subtree are cleaned up.
    void on_entity_tree_destroyed(World& world, entt::entity entity);

    /// Called before a World is destroyed or replaced (level/project switch).
    /// Invokes on_destroy for all script entities belonging to that world and clears them.
    void on_world_destroyed(World& world);

    /// Reload a specific entity's script from disk (hot-reload).
    Result<> reload_script(World& world, entt::entity entity);

    /// Shutdown the Lua VM and release all resources.
    void shutdown();

    /// Number of currently active script environments (for diagnostics UI).
    std::size_t get_environment_count() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

NOC_RESTORE_DLL_WARNINGS
