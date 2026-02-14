#include "../Public/ScriptEngine.hpp"
#include "../Public/LuaBindings.hpp"

#include <ECS/Public/Components.hpp>
#include <ECS/Public/World.hpp>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <fmt/core.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ── Entity wrapper for Lua ────────────────────────────────────────────
// Provides a safe, limited view of an entity to Lua scripts.

struct LuaEntity
{
    entt::entity handle{entt::null};
    World* world{nullptr};

    bool valid() const noexcept
    {
        return world && handle != entt::null && world->registry().valid(handle);
    }

    std::string name() const
    {
        if (!valid())
            return "";
        const auto* nc = world->registry().try_get<NameComponent>(handle);
        return nc ? nc->name : "";
    }

    TransformComponent* transform() const
    {
        if (!valid())
            return nullptr;
        return world->registry().try_get<TransformComponent>(handle);
    }

    bool has_mesh() const
    {
        if (!valid())
            return false;
        return world->registry().any_of<MeshComponent>(handle);
    }

    uint32_t id() const noexcept
    {
        return static_cast<uint32_t>(handle);
    }
};

// ── Pimpl implementation ──────────────────────────────────────────────

struct ScriptEngine::Impl
{
    sol::state lua;
    struct ScriptEnvironment
    {
        sol::environment environment;
        World* world{nullptr};
    };
    std::unordered_map<entt::entity, ScriptEnvironment> environments;
    fs::path scriptRoot; // base directory for resolving relative script paths

    fs::path resolve_script_path(std::string_view scriptPath) const
    {
        fs::path p(scriptPath);
        if (p.is_absolute())
            return p;
        // Resolve relative to the project root (script root)
        if (!scriptRoot.empty())
        {
            fs::path resolved = scriptRoot / p;
            if (fs::exists(resolved))
                return resolved;
        }
        // Fall back to relative path (resolved against CWD)
        return p;
    }
};

// ── Constructor / destructor / move ───────────────────────────────────

ScriptEngine::ScriptEngine() : m_impl(std::make_unique<Impl>())
{}

ScriptEngine::~ScriptEngine()
{
    if (m_impl)
        shutdown();
}

ScriptEngine::ScriptEngine(ScriptEngine&&) noexcept = default;
ScriptEngine& ScriptEngine::operator=(ScriptEngine&&) noexcept = default;

// ── initialize ────────────────────────────────────────────────────────

Result<> ScriptEngine::initialize()
{
    auto& lua = m_impl->lua;

    // Open standard Lua libraries + LuaJIT's jit and ffi modules
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::os, sol::lib::jit,
                       sol::lib::ffi);

    // Verify that we're running LuaJIT
    sol::object jitTable = lua["jit"];
    if (jitTable.valid())
    {
        sol::table jit = jitTable;
        std::string ver = jit["version"];
        fmt::print("[ScriptEngine] Running: {}\n", ver);
    }
    else
    {
        fmt::print("[ScriptEngine] Warning: LuaJIT not detected, running in standard Lua mode (no JIT compilation)\n");
    }

    // Override print() to go through fmt
    lua.set_function("print", [](sol::variadic_args va) {
        std::string msg;
        for (size_t i = 0; i < va.size(); ++i)
        {
            if (i > 0)
                msg += "\t";
            sol::object obj = va[i];
            if (obj.is<std::string>())
                msg += obj.as<std::string>();
            else if (obj.is<double>())
                msg += fmt::format("{}", obj.as<double>());
            else if (obj.is<bool>())
                msg += obj.as<bool>() ? "true" : "false";
            else if (obj.is<sol::nil_t>())
                msg += "nil";
            else
                msg += fmt::format("[{}]", sol::type_name(va.lua_state(), obj.get_type()));
        }
        fmt::print("[Lua] {}\n", msg);
    });

    // ── Register Vec3 ─────────────────────────────────────────────────

    lua.new_usertype<LuaVec3>(
        "Vec3", sol::constructors<LuaVec3(), LuaVec3(float, float, float)>(), "x", &LuaVec3::x, "y", &LuaVec3::y, "z",
        &LuaVec3::z, sol::meta_function::addition, &LuaVec3::operator+, sol::meta_function::subtraction,
        sol::resolve<LuaVec3(const LuaVec3&) const>(&LuaVec3::operator-), sol::meta_function::multiplication,
        &LuaVec3::operator*, sol::meta_function::unary_minus, sol::resolve<LuaVec3() const>(&LuaVec3::operator-),
        "length", &LuaVec3::length, "normalized", &LuaVec3::normalized, "dot", &LuaVec3::dot, "cross", &LuaVec3::cross);

    // ── Register Transform ────────────────────────────────────────────
    // Lua gets a pointer to the actual TransformComponent on the entity,
    // so mutations in Lua directly affect the C++ side.

    lua.new_usertype<TransformComponent>(
        "Transform", sol::no_constructor, "position",
        sol::property([](const TransformComponent& t) { return LuaVec3(t.position); },
                      [](TransformComponent& t, const LuaVec3& v) { t.position = v.to_dx(); }),
        "scale",
        sol::property([](const TransformComponent& t) { return LuaVec3(t.scale); },
                      [](TransformComponent& t, const LuaVec3& v) { t.scale = v.to_dx(); }),
        "get_rotation_euler",
        [](const TransformComponent& t) -> LuaVec3 {
            auto e = t.get_rotation_euler();
            return LuaVec3(e);
        },
        "set_rotation_euler",
        [](TransformComponent& t, float pitch, float yaw, float roll) { t.set_rotation_euler(pitch, yaw, roll); });

    // ── Register Entity ───────────────────────────────────────────────

    lua.new_usertype<LuaEntity>("Entity", sol::no_constructor, "name", &LuaEntity::name, "transform",
                                &LuaEntity::transform, "has_mesh", &LuaEntity::has_mesh, "id", &LuaEntity::id, "valid",
                                &LuaEntity::valid);

    fmt::print("[ScriptEngine] Initialized LuaJIT VM\n");
    return {};
}

// ── set_script_root ───────────────────────────────────────────────────

void ScriptEngine::set_script_root(const fs::path& root)
{
    m_impl->scriptRoot = root;
}

// ── load_script ───────────────────────────────────────────────────────

Result<> ScriptEngine::load_script(World& world, entt::entity entity)
{
    auto& reg = world.registry();
    auto* sc = reg.try_get<ScriptComponent>(entity);
    if (!sc)
        return make_error("Entity has no ScriptComponent", ErrorCode::AssetInvalidData);

    fs::path scriptPath = m_impl->resolve_script_path(sc->scriptPath);

    if (!fs::exists(scriptPath))
        return make_error(fmt::format("Script file not found: {}", scriptPath.string()), ErrorCode::AssetFileNotFound);

    // Read script source
    std::ifstream file(scriptPath);
    if (!file)
        return make_error(fmt::format("Failed to open script: {}", scriptPath.string()), ErrorCode::FileReadFailed);

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    auto& lua = m_impl->lua;

    // Create a sandboxed environment for this entity
    sol::environment env(lua, sol::create, lua.globals());
    m_impl->environments[entity] = {env, &world};

    // Execute the script within the environment
    auto loadResult = lua.safe_script(source, env, sol::script_pass_on_error, scriptPath.string());
    if (!loadResult.valid())
    {
        sol::error err = loadResult;
        m_impl->environments.erase(entity);
        return make_error(fmt::format("Lua load error in '{}': {}", sc->scriptPath, err.what()),
                          ErrorCode::AssetParsingFailed);
    }

    sc->initialized = false;
    return {};
}

// ── update ────────────────────────────────────────────────────────────

void ScriptEngine::update(World& world, float dt)
{
    auto& reg = world.registry();
    auto view = reg.view<ScriptComponent>();

    for (auto entity : view)
    {
        auto& sc = view.get<ScriptComponent>(entity);
        if (sc.scriptPath.empty())
            continue;

        // Lazy-load: if no environment exists yet, load the script
        auto envIt = m_impl->environments.find(entity);
        if (envIt == m_impl->environments.end())
        {
            auto result = load_script(world, entity);
            if (!result)
            {
                fmt::print("Warning: {}\n", result.error().message);
                // Prevent retrying every frame — clear the path
                continue;
            }
            envIt = m_impl->environments.find(entity);
            if (envIt == m_impl->environments.end())
                continue;
        }

        auto& envRecord = envIt->second;
        if (envRecord.world != &world)
        {
            // Entity IDs can be reused across worlds; stale environments must be discarded.
            m_impl->environments.erase(envIt);
            auto result = load_script(world, entity);
            if (!result)
            {
                fmt::print("Warning: {}\n", result.error().message);
                continue;
            }
            envIt = m_impl->environments.find(entity);
            if (envIt == m_impl->environments.end())
                continue;
        }
        auto& env = envIt->second.environment;
        LuaEntity luaEntity{entity, &world};

        // Call on_start() once
        if (!sc.initialized)
        {
            sol::protected_function onStart = env["on_start"];
            if (onStart.valid())
            {
                auto result = onStart(luaEntity);
                if (!result.valid())
                {
                    sol::error err = result;
                    fmt::print("[Lua Error] on_start in '{}': {}\n", sc.scriptPath, err.what());
                }
            }
            sc.initialized = true;
        }

        // Call on_update() every frame
        sol::protected_function onUpdate = env["on_update"];
        if (onUpdate.valid())
        {
            auto result = onUpdate(luaEntity, dt);
            if (!result.valid())
            {
                sol::error err = result;
                fmt::print("[Lua Error] on_update in '{}': {}\n", sc.scriptPath, err.what());
            }
        }
    }
}

// ── on_entity_destroyed ───────────────────────────────────────────────

void ScriptEngine::on_entity_destroyed(World& world, entt::entity entity)
{
    auto envIt = m_impl->environments.find(entity);
    if (envIt == m_impl->environments.end())
        return;

    if (envIt->second.world != &world)
        return;

    auto& env = envIt->second.environment;
    LuaEntity luaEntity{entity, &world};

    // Call on_destroy() if defined
    sol::protected_function onDestroy = env["on_destroy"];
    if (onDestroy.valid())
    {
        auto result = onDestroy(luaEntity);
        if (!result.valid())
        {
            sol::error err = result;
            auto* sc = world.registry().try_get<ScriptComponent>(entity);
            std::string path = sc ? sc->scriptPath : "unknown";
            fmt::print("[Lua Error] on_destroy in '{}': {}\n", path, err.what());
        }
    }

    m_impl->environments.erase(envIt);
}

void ScriptEngine::on_entity_tree_destroyed(World& world, entt::entity entity)
{
    auto& reg = world.registry();
    if (!reg.valid(entity))
        return;

    std::vector<entt::entity> stack;
    stack.push_back(entity);
    while (!stack.empty())
    {
        entt::entity current = stack.back();
        stack.pop_back();

        if (!reg.valid(current))
            continue;

        if (const auto* hierarchy = reg.try_get<HierarchyComponent>(current))
        {
            for (entt::entity child : hierarchy->children)
            {
                if (reg.valid(child))
                    stack.push_back(child);
            }
        }

        on_entity_destroyed(world, current);
    }
}

void ScriptEngine::on_world_destroyed(World& world)
{
    if (!m_impl)
        return;

    // Collect keys first to avoid iterator invalidation while erasing.
    std::vector<entt::entity> entities;
    entities.reserve(m_impl->environments.size());
    for (const auto& [entity, envRecord] : m_impl->environments)
    {
        if (envRecord.world == &world)
            entities.push_back(entity);
    }

    for (entt::entity entity : entities)
        on_entity_destroyed(world, entity);

    // In case some entities were already invalid and did not invoke on_destroy.
    for (auto it = m_impl->environments.begin(); it != m_impl->environments.end();)
    {
        if (it->second.world == &world)
            it = m_impl->environments.erase(it);
        else
            ++it;
    }
}

// ── reload_script ─────────────────────────────────────────────────────

Result<> ScriptEngine::reload_script(World& world, entt::entity entity)
{
    // Clean up existing environment
    auto envIt = m_impl->environments.find(entity);
    if (envIt != m_impl->environments.end() && envIt->second.world == &world)
        m_impl->environments.erase(envIt);

    // Reset initialized flag
    auto* sc = world.registry().try_get<ScriptComponent>(entity);
    if (sc)
        sc->initialized = false;

    // Re-load
    return load_script(world, entity);
}

// ── shutdown ──────────────────────────────────────────────────────────

void ScriptEngine::shutdown()
{
    if (!m_impl)
        return;

    // Clear all environments before closing the Lua state
    m_impl->environments.clear();

    // sol::state destructor handles lua_close
    fmt::print("[ScriptEngine] Shutdown\n");
}

std::size_t ScriptEngine::get_environment_count() const noexcept
{
    if (!m_impl)
        return 0;
    return m_impl->environments.size();
}
