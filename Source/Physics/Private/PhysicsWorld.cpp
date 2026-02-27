#include "../Public/PhysicsWorld.hpp"

#include <ECS/Public/Components.hpp>
#include <ECS/Public/World.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fmt/core.h>

namespace
{
namespace Layers
{
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr std::uint32_t NUM_LAYERS = 2;
} // namespace Layers

namespace BroadPhaseLayers
{
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr std::uint32_t NUM_LAYERS = 2;
} // namespace BroadPhaseLayers

static std::uint32_t entity_key(entt::entity entity) noexcept
{
    return static_cast<std::uint32_t>(entity);
}

static JPH::EMotionType to_motion_type(PhysicsBodyMotionType motionType) noexcept
{
    switch (motionType)
    {
    case PhysicsBodyMotionType::Static:
        return JPH::EMotionType::Static;
    case PhysicsBodyMotionType::Kinematic:
        return JPH::EMotionType::Kinematic;
    case PhysicsBodyMotionType::Dynamic:
    default:
        return JPH::EMotionType::Dynamic;
    }
}

static JPH::ObjectLayer to_object_layer(PhysicsBodyMotionType motionType) noexcept
{
    return motionType == PhysicsBodyMotionType::Static ? Layers::NON_MOVING : Layers::MOVING;
}

static std::array<float, 3> to_scale_key(const DirectX::XMFLOAT3& scale) noexcept
{
    return {scale.x, scale.y, scale.z};
}

static bool scale_changed(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept
{
    constexpr float eps = 0.0001f;
    return std::abs(a[0] - b[0]) > eps || std::abs(a[1] - b[1]) > eps || std::abs(a[2] - b[2]) > eps;
}

struct PhysicsStateKey
{
    PhysicsBodyMotionType motionType{PhysicsBodyMotionType::Dynamic};
    DirectX::XMFLOAT3 halfExtents{0.5f, 0.5f, 0.5f};
    float friction{0.5f};
    float restitution{0.0f};
    bool useGravity{true};
    float linearDamping{0.05f};
    float angularDamping{0.05f};
};

static PhysicsStateKey make_state_key(const PhysicsBodyComponent& bodyComp) noexcept
{
    PhysicsStateKey key{};
    key.motionType = bodyComp.motionType;
    key.halfExtents = bodyComp.halfExtents;
    key.friction = bodyComp.friction;
    key.restitution = bodyComp.restitution;
    key.useGravity = bodyComp.useGravity;
    key.linearDamping = bodyComp.linearDamping;
    key.angularDamping = bodyComp.angularDamping;
    return key;
}

static bool state_key_changed(const PhysicsStateKey& a, const PhysicsStateKey& b) noexcept
{
    constexpr float eps = 0.0001f;
    return a.motionType != b.motionType || std::abs(a.halfExtents.x - b.halfExtents.x) > eps ||
           std::abs(a.halfExtents.y - b.halfExtents.y) > eps || std::abs(a.halfExtents.z - b.halfExtents.z) > eps ||
           std::abs(a.friction - b.friction) > eps || std::abs(a.restitution - b.restitution) > eps ||
           a.useGravity != b.useGravity || std::abs(a.linearDamping - b.linearDamping) > eps ||
           std::abs(a.angularDamping - b.angularDamping) > eps;
}

static void jolt_trace_impl(const char* inFMT, ...)
{
    char buffer[1024]{};
    va_list args;
    va_start(args, inFMT);
    std::vsnprintf(buffer, sizeof(buffer), inFMT, args);
    va_end(args);
    fmt::print("[Jolt] {}\n", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool jolt_assert_failed_impl(const char* inExpression, const char* inMessage, const char* inFile,
                                    JPH::uint inLine)
{
    fmt::print("[Jolt Assert] {}:{} | {} | {}\n", inFile, static_cast<unsigned>(inLine),
               inExpression ? inExpression : "(null)", inMessage ? inMessage : "");
    return true;
}
#endif
} // namespace

struct PhysicsWorld::Impl
{
    static constexpr std::uint32_t MaxBodies = 65536;
    static constexpr std::uint32_t NumBodyMutexes = 0;
    static constexpr std::uint32_t MaxBodyPairs = 65536;
    static constexpr std::uint32_t MaxContactConstraints = 65536;
    static constexpr std::uint32_t TempAllocatorBytes = 64 * 1024 * 1024;

    bool initialized{false};
    bool enabled{false};
    bool wasEnabledLastStep{false};
    float accumulator{0.0f};
    float fixedStep{1.0f / 60.0f};
    std::uint32_t maxSubSteps{4};

    std::unique_ptr<JPH::BroadPhaseLayerInterfaceTable> broadPhaseLayerInterface;
    std::unique_ptr<JPH::ObjectLayerPairFilterTable> objectLayerPairFilter;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> objectVsBroadPhaseFilter;

    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    JPH::PhysicsSystem physicsSystem;

    std::unordered_map<std::uint32_t, JPH::BodyID> bodies;
    std::unordered_map<std::uint32_t, std::array<float, 3>> bodyShapeScale;
    std::unordered_map<std::uint32_t, PhysicsStateKey> bodyState;

    void configure_layers()
    {
        broadPhaseLayerInterface =
            std::make_unique<JPH::BroadPhaseLayerInterfaceTable>(Layers::NUM_LAYERS, BroadPhaseLayers::NUM_LAYERS);
        objectLayerPairFilter = std::make_unique<JPH::ObjectLayerPairFilterTable>(Layers::NUM_LAYERS);

        broadPhaseLayerInterface->MapObjectToBroadPhaseLayer(Layers::NON_MOVING, BroadPhaseLayers::NON_MOVING);
        broadPhaseLayerInterface->MapObjectToBroadPhaseLayer(Layers::MOVING, BroadPhaseLayers::MOVING);

        objectLayerPairFilter->EnableCollision(Layers::MOVING, Layers::MOVING);
        objectLayerPairFilter->EnableCollision(Layers::MOVING, Layers::NON_MOVING);

        objectVsBroadPhaseFilter = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
            *broadPhaseLayerInterface,
            BroadPhaseLayers::NUM_LAYERS,
            *objectLayerPairFilter,
            Layers::NUM_LAYERS);
    }

    void destroy_body(JPH::BodyID id)
    {
        JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
        if (bodyInterface.IsAdded(id))
            bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
    }

    void recreate_body(PhysicsBodyComponent& bodyComp, const TransformComponent& transform, entt::entity entity)
    {
        const auto key = entity_key(entity);
        if (auto bodyIt = bodies.find(key); bodyIt != bodies.end())
        {
            destroy_body(bodyIt->second);
            bodies.erase(bodyIt);
        }

        const float scaledX = std::max(bodyComp.halfExtents.x * std::max(std::abs(transform.scale.x), 0.0001f), 0.01f);
        const float scaledY = std::max(bodyComp.halfExtents.y * std::max(std::abs(transform.scale.y), 0.0001f), 0.01f);
        const float scaledZ = std::max(bodyComp.halfExtents.z * std::max(std::abs(transform.scale.z), 0.0001f), 0.01f);
        const JPH::Vec3 halfExtents(scaledX, scaledY, scaledZ);
        JPH::ShapeRefC shape = new JPH::BoxShape(halfExtents);

        const JPH::EMotionType motionType = to_motion_type(bodyComp.motionType);
        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3(transform.position.x, transform.position.y, transform.position.z),
            JPH::Quat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w),
            motionType,
            to_object_layer(bodyComp.motionType));

        settings.mFriction = std::clamp(bodyComp.friction, 0.0f, 1.0f);
        settings.mRestitution = std::clamp(bodyComp.restitution, 0.0f, 1.0f);
        settings.mLinearDamping = std::max(bodyComp.linearDamping, 0.0f);
        settings.mAngularDamping = std::max(bodyComp.angularDamping, 0.0f);
        settings.mGravityFactor = bodyComp.useGravity ? 1.0f : 0.0f;

        JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
        JPH::Body* body = bodyInterface.CreateBody(settings);
        if (!body)
            return;

        const JPH::BodyID bodyId = body->GetID();
        const JPH::EActivation activation = (motionType == JPH::EMotionType::Static) ? JPH::EActivation::DontActivate
                                                                                       : JPH::EActivation::Activate;
        bodyInterface.AddBody(bodyId, activation);

        bodies[key] = bodyId;
        bodyShapeScale[key] = to_scale_key(transform.scale);
        bodyState[key] = make_state_key(bodyComp);
        bodyComp.runtimeDirty = false;
        bodyComp.runtimeInitialized = true;
    }

    void sync_entities(World& world)
    {
        auto& reg = world.registry();

        for (auto it = bodies.begin(); it != bodies.end();)
        {
            const entt::entity entity = static_cast<entt::entity>(it->first);
            if (!reg.valid(entity) || !reg.all_of<PhysicsBodyComponent, TransformComponent>(entity))
            {
                destroy_body(it->second);
                bodyShapeScale.erase(it->first);
                bodyState.erase(it->first);
                it = bodies.erase(it);
                continue;
            }

            const auto& bodyComp = reg.get<PhysicsBodyComponent>(entity);
            if (!bodyComp.enabled)
            {
                destroy_body(it->second);
                bodyShapeScale.erase(it->first);
                bodyState.erase(it->first);
                it = bodies.erase(it);
                continue;
            }

            ++it;
        }

        auto view = reg.view<PhysicsBodyComponent, TransformComponent>();
        for (entt::entity entity : view)
        {
            auto& bodyComp = view.get<PhysicsBodyComponent>(entity);
            const auto& transform = view.get<TransformComponent>(entity);

            if (!bodyComp.enabled)
                continue;

            const auto scaleKey = to_scale_key(transform.scale);
            const auto scaleIt = bodyShapeScale.find(entity_key(entity));
            if (scaleIt == bodyShapeScale.end() || scale_changed(scaleIt->second, scaleKey))
                bodyComp.runtimeDirty = true;

            const auto stateIt = bodyState.find(entity_key(entity));
            const auto newState = make_state_key(bodyComp);
            if (stateIt == bodyState.end() || state_key_changed(stateIt->second, newState))
                bodyComp.runtimeDirty = true;

            auto bodyIt = bodies.find(entity_key(entity));
            if (bodyIt == bodies.end() || bodyComp.runtimeDirty)
                recreate_body(bodyComp, transform, entity);

            bodyIt = bodies.find(entity_key(entity));
            if (bodyIt == bodies.end())
                continue;

            const JPH::EMotionType motionType = to_motion_type(bodyComp.motionType);
            if (motionType == JPH::EMotionType::Static || motionType == JPH::EMotionType::Kinematic)
            {
                JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
                bodyInterface.SetPositionAndRotationWhenChanged(
                    bodyIt->second,
                    JPH::RVec3(transform.position.x, transform.position.y, transform.position.z),
                    JPH::Quat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w),
                    JPH::EActivation::DontActivate);
            }
            else if (!enabled)
            {
                // Keep dynamic bodies in editor-authored pose while simulation is disabled.
                JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
                bodyInterface.SetPositionAndRotationWhenChanged(
                    bodyIt->second,
                    JPH::RVec3(transform.position.x, transform.position.y, transform.position.z),
                    JPH::Quat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w),
                    JPH::EActivation::DontActivate);
                bodyInterface.SetLinearAndAngularVelocity(bodyIt->second, JPH::Vec3::sZero(), JPH::Vec3::sZero());
            }
        }
    }
};

PhysicsWorld::PhysicsWorld() : m_impl(std::make_unique<Impl>())
{}

PhysicsWorld::~PhysicsWorld()
{
    shutdown();
}

PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

::Result<> PhysicsWorld::initialize()
{
    if (!m_impl)
        m_impl = std::make_unique<Impl>();

    if (m_impl->initialized)
        return {};

    JPH::Trace = jolt_trace_impl;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = jolt_assert_failed_impl;
#endif

    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    m_impl->configure_layers();

    m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(Impl::TempAllocatorBytes);

    const std::uint32_t hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    const std::uint32_t workerThreads = (hardwareThreads > 1) ? (hardwareThreads - 1) : 1;
    m_impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        static_cast<int>(workerThreads));

    m_impl->physicsSystem.Init(
        Impl::MaxBodies,
        Impl::NumBodyMutexes,
        Impl::MaxBodyPairs,
        Impl::MaxContactConstraints,
        *m_impl->broadPhaseLayerInterface,
        *m_impl->objectVsBroadPhaseFilter,
        *m_impl->objectLayerPairFilter);

    m_impl->initialized = true;
    return {};
}

void PhysicsWorld::shutdown()
{
    if (!m_impl || !m_impl->initialized)
        return;

    clear();

    m_impl->jobSystem.reset();
    m_impl->tempAllocator.reset();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    m_impl->initialized = false;
}

void PhysicsWorld::set_enabled(bool enabled) noexcept
{
    if (!m_impl)
        return;
    m_impl->enabled = enabled;
}

bool PhysicsWorld::is_enabled() const noexcept
{
    return m_impl && m_impl->enabled;
}

void PhysicsWorld::step(World& world, float deltaTime)
{
    if (!m_impl || !m_impl->initialized)
        return;

    const bool enableTransition = m_impl->enabled && !m_impl->wasEnabledLastStep;
    if (enableTransition)
    {
        m_impl->accumulator = 0.0f;

        // Recreate bodies from latest entity transforms to avoid teleports to stale poses.
        auto view = world.registry().view<PhysicsBodyComponent>();
        for (entt::entity entity : view)
            view.get<PhysicsBodyComponent>(entity).runtimeDirty = true;
    }

    m_impl->sync_entities(world);

    if (!m_impl->enabled)
    {
        m_impl->wasEnabledLastStep = false;
        return;
    }

    m_impl->accumulator += std::max(0.0f, deltaTime);

    std::uint32_t steps = 0;
    while (m_impl->accumulator >= m_impl->fixedStep && steps < m_impl->maxSubSteps)
    {
        m_impl->physicsSystem.Update(m_impl->fixedStep, 1, m_impl->tempAllocator.get(), m_impl->jobSystem.get());
        m_impl->accumulator -= m_impl->fixedStep;
        ++steps;
    }

    auto& reg = world.registry();
    auto view = reg.view<PhysicsBodyComponent, TransformComponent>();

    bool anyTransformUpdated = false;
    JPH::BodyInterface& bodyInterface = m_impl->physicsSystem.GetBodyInterface();

    for (entt::entity entity : view)
    {
        auto& bodyComp = view.get<PhysicsBodyComponent>(entity);
        if (!bodyComp.enabled || bodyComp.motionType != PhysicsBodyMotionType::Dynamic)
            continue;

        auto bodyIt = m_impl->bodies.find(entity_key(entity));
        if (bodyIt == m_impl->bodies.end())
            continue;

        auto& transform = view.get<TransformComponent>(entity);
        const JPH::RVec3 position = bodyInterface.GetPosition(bodyIt->second);
        const JPH::Quat rotation = bodyInterface.GetRotation(bodyIt->second);

        transform.position = {
            static_cast<float>(position.GetX()),
            static_cast<float>(position.GetY()),
            static_cast<float>(position.GetZ()),
        };
        transform.rotation = {rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW()};
        anyTransformUpdated = true;
    }

    if (anyTransformUpdated)
        world.mark_transforms_dirty();

    m_impl->wasEnabledLastStep = true;
}

void PhysicsWorld::clear()
{
    if (!m_impl || !m_impl->initialized)
        return;

    for (const auto& [entity, bodyId] : m_impl->bodies)
    {
        (void)entity;
        m_impl->destroy_body(bodyId);
    }

    m_impl->bodies.clear();
    m_impl->bodyShapeScale.clear();
    m_impl->bodyState.clear();
    m_impl->accumulator = 0.0f;
}

void PhysicsWorld::on_entity_tree_destroyed(World& world, entt::entity entity)
{
    if (!m_impl || !m_impl->initialized)
        return;

    auto& reg = world.registry();
    if (!reg.valid(entity))
        return;

    std::vector<entt::entity> stack{entity};

    while (!stack.empty())
    {
        const entt::entity current = stack.back();
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

        on_entity_removed(current);
    }
}

void PhysicsWorld::on_entity_removed(entt::entity entity)
{
    if (!m_impl || !m_impl->initialized)
        return;

    auto bodyIt = m_impl->bodies.find(entity_key(entity));
    if (bodyIt == m_impl->bodies.end())
        return;

    m_impl->destroy_body(bodyIt->second);
    m_impl->bodyShapeScale.erase(bodyIt->first);
    m_impl->bodyState.erase(bodyIt->first);
    m_impl->bodies.erase(bodyIt);
}
