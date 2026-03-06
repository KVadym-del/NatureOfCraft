#include "../Public/PhysicsWorld.hpp"

#include <ECS/Public/Components.hpp>
#include <ECS/Public/World.hpp>
#include <Assets/Public/AssetManager.hpp>

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
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
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
    PhysicsColliderShapeType shapeType{PhysicsColliderShapeType::Box};
    DirectX::XMFLOAT3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius{0.5f};
    float halfHeight{0.5f};
    DirectX::XMFLOAT3 colliderOffset{0.0f, 0.0f, 0.0f};
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
    key.shapeType = bodyComp.shapeType;
    key.halfExtents = bodyComp.halfExtents;
    key.radius = bodyComp.radius;
    key.halfHeight = bodyComp.halfHeight;
    key.colliderOffset = bodyComp.colliderOffset;
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
    return a.motionType != b.motionType || a.shapeType != b.shapeType ||
           std::abs(a.halfExtents.x - b.halfExtents.x) > eps ||
           std::abs(a.halfExtents.y - b.halfExtents.y) > eps || std::abs(a.halfExtents.z - b.halfExtents.z) > eps ||
           std::abs(a.radius - b.radius) > eps || std::abs(a.halfHeight - b.halfHeight) > eps ||
           std::abs(a.colliderOffset.x - b.colliderOffset.x) > eps ||
           std::abs(a.colliderOffset.y - b.colliderOffset.y) > eps ||
           std::abs(a.colliderOffset.z - b.colliderOffset.z) > eps ||
           std::abs(a.friction - b.friction) > eps || std::abs(a.restitution - b.restitution) > eps ||
           a.useGravity != b.useGravity || std::abs(a.linearDamping - b.linearDamping) > eps ||
           std::abs(a.angularDamping - b.angularDamping) > eps;
}

static JPH::Quat to_jolt_quat(const DirectX::XMFLOAT4& rotation) noexcept
{
    return JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w);
}

static DirectX::XMFLOAT3 rotate_vector(const DirectX::XMFLOAT3& vector, const DirectX::XMFLOAT4& rotation) noexcept
{
    using namespace DirectX;
    const XMVECTOR local = XMVectorSet(vector.x, vector.y, vector.z, 0.0f);
    const XMVECTOR q = XMLoadFloat4(&rotation);
    XMFLOAT3 out{};
    XMStoreFloat3(&out, XMVector3Rotate(local, q));
    return out;
}

static JPH::RVec3 to_body_position(const TransformComponent& transform, const PhysicsBodyComponent& bodyComp) noexcept
{
    const DirectX::XMFLOAT3 rotatedOffset = rotate_vector(bodyComp.colliderOffset, transform.rotation);
    return JPH::RVec3(transform.position.x + rotatedOffset.x,
                      transform.position.y + rotatedOffset.y,
                      transform.position.z + rotatedOffset.z);
}

static DirectX::XMFLOAT3 to_entity_position(const JPH::RVec3& bodyPosition,
                                            const DirectX::XMFLOAT4& rotation,
                                            const DirectX::XMFLOAT3& colliderOffset) noexcept
{
    const DirectX::XMFLOAT3 rotatedOffset = rotate_vector(colliderOffset, rotation);
    return {
        static_cast<float>(bodyPosition.GetX()) - rotatedOffset.x,
        static_cast<float>(bodyPosition.GetY()) - rotatedOffset.y,
        static_cast<float>(bodyPosition.GetZ()) - rotatedOffset.z,
    };
}

static JPH::ShapeRefC create_primitive_shape(const PhysicsBodyComponent& bodyComp, const TransformComponent& transform)
{
    const float scaleX = std::max(std::abs(transform.scale.x), 0.0001f);
    const float scaleY = std::max(std::abs(transform.scale.y), 0.0001f);
    const float scaleZ = std::max(std::abs(transform.scale.z), 0.0001f);

    switch (bodyComp.shapeType)
    {
    case PhysicsColliderShapeType::Sphere:
    {
        const float sphereScale = std::max({scaleX, scaleY, scaleZ});
        const float radius = std::max(bodyComp.radius * sphereScale, 0.01f);
        return new JPH::SphereShape(radius);
    }
    case PhysicsColliderShapeType::Capsule:
    {
        const float radialScale = std::max(scaleX, scaleZ);
        const float radius = std::max(bodyComp.radius * radialScale, 0.01f);
        const float halfHeight = std::max(bodyComp.halfHeight * scaleY, 0.01f);
        return new JPH::CapsuleShape(halfHeight, radius);
    }
    case PhysicsColliderShapeType::Cylinder:
    {
        const float radialScale = std::max(scaleX, scaleZ);
        const float radius = std::max(bodyComp.radius * radialScale, 0.01f);
        const float halfHeight = std::max(bodyComp.halfHeight * scaleY, 0.01f);
        return new JPH::CylinderShape(halfHeight, radius);
    }
    case PhysicsColliderShapeType::Mesh:
    case PhysicsColliderShapeType::ConvexHull:
    case PhysicsColliderShapeType::StaticCompound:
    case PhysicsColliderShapeType::MutableCompound:
    case PhysicsColliderShapeType::Box:
    default:
    {
        const float scaledX = std::max(bodyComp.halfExtents.x * scaleX, 0.01f);
        const float scaledY = std::max(bodyComp.halfExtents.y * scaleY, 0.01f);
        const float scaledZ = std::max(bodyComp.halfExtents.z * scaleZ, 0.01f);
        return new JPH::BoxShape(JPH::Vec3(scaledX, scaledY, scaledZ));
    }
    }
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
    AssetManager assetManager;
    std::filesystem::path assetRoot;

    std::filesystem::path resolve_asset_load_path(const std::string& assetPath) const
    {
        std::filesystem::path path(assetPath);
        if (path.empty())
            return {};
        if (path.is_absolute())
            return path;

        if (!assetRoot.empty())
        {
            const std::filesystem::path projectPath = assetRoot / path;
            std::error_code ec;
            if (std::filesystem::exists(projectPath, ec))
                return projectPath;
        }

        std::error_code ec;
        if (std::filesystem::exists(path, ec))
            return path;

        auto absolute = std::filesystem::absolute(path, ec);
        if (!ec && std::filesystem::exists(absolute, ec))
            return absolute;

        return path;
    }

    static JPH::ShapeRefC create_shape_from_result(const JPH::ShapeSettings::ShapeResult& shapeResult)
    {
        if (!shapeResult.IsValid())
            return {};
        return shapeResult.Get();
    }

    JPH::ShapeRefC create_mesh_shape_from_mesh_data(const MeshData& meshData, const DirectX::XMMATRIX& localToBody)
    {
        if (meshData.vertices.empty() || meshData.indices.size() < 3)
            return {};

        using namespace DirectX;
        JPH::VertexList vertices;
        vertices.reserve(meshData.vertices.size());
        for (const auto& vertex : meshData.vertices)
        {
            const XMVECTOR p = XMVectorSet(vertex.pos.x, vertex.pos.y, vertex.pos.z, 1.0f);
            XMFLOAT3 tp{};
            XMStoreFloat3(&tp, XMVector3TransformCoord(p, localToBody));
            vertices.push_back(JPH::Float3(tp.x, tp.y, tp.z));
        }

        JPH::IndexedTriangleList triangles;
        triangles.reserve(meshData.indices.size() / 3);
        for (size_t i = 0; i + 2 < meshData.indices.size(); i += 3)
            triangles.emplace_back(meshData.indices[i], meshData.indices[i + 1], meshData.indices[i + 2]);

        JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
        return create_shape_from_result(settings.Create());
    }

    JPH::ShapeRefC create_convex_hull_shape_from_mesh_data(const MeshData& meshData,
                                                           const DirectX::XMMATRIX& localToBody)
    {
        if (meshData.vertices.empty())
            return {};

        using namespace DirectX;
        JPH::Array<JPH::Vec3> points;
        points.reserve(meshData.vertices.size());
        for (const auto& vertex : meshData.vertices)
        {
            const XMVECTOR p = XMVectorSet(vertex.pos.x, vertex.pos.y, vertex.pos.z, 1.0f);
            XMFLOAT3 tp{};
            XMStoreFloat3(&tp, XMVector3TransformCoord(p, localToBody));
            points.push_back(JPH::Vec3(tp.x, tp.y, tp.z));
        }

        JPH::ConvexHullShapeSettings settings(points, 0.02f);
        return create_shape_from_result(settings.Create());
    }

    static const MeshData* find_matching_mesh(const ModelData& modelData, const std::string& meshName)
    {
        if (!meshName.empty())
        {
            for (const auto& mesh : modelData.meshes)
            {
                if (mesh.name == meshName)
                    return &mesh;
            }
        }

        return modelData.meshes.empty() ? nullptr : &modelData.meshes.front();
    }

    static bool find_mesh_source_in_subtree(World& world,
                                            entt::entity root,
                                            const MeshComponent*& outMeshComp,
                                            std::string& outMeshName,
                                            DirectX::XMMATRIX& outLocalToRoot)
    {
        using namespace DirectX;
        auto& reg = world.registry();
        std::vector<std::pair<entt::entity, XMMATRIX>> stack{{root, XMMatrixIdentity()}};

        while (!stack.empty())
        {
            const entt::entity current = stack.back().first;
            const XMMATRIX parentToRoot = stack.back().second;
            stack.pop_back();

            if (!reg.valid(current))
                continue;

            if (const auto* meshComp = reg.try_get<MeshComponent>(current))
            {
                if (!meshComp->assetPath.empty())
                {
                    outMeshComp = meshComp;
                    if (const auto* nameComp = reg.try_get<NameComponent>(current))
                        outMeshName = nameComp->name;
                    else
                        outMeshName.clear();
                    outLocalToRoot = parentToRoot;
                    return true;
                }
            }

            if (const auto* hierarchy = reg.try_get<HierarchyComponent>(current))
            {
                for (entt::entity child : hierarchy->children)
                {
                    if (reg.valid(child))
                    {
                        XMMATRIX childToRoot = parentToRoot;
                        if (const auto* childTransform = reg.try_get<TransformComponent>(child))
                            childToRoot = XMMatrixMultiply(childTransform->get_local_matrix(), parentToRoot);
                        stack.emplace_back(child, childToRoot);
                    }
                }
            }
        }

        return false;
    }

    JPH::ShapeRefC create_model_shape(World& world,
                                      entt::entity entity,
                                      const PhysicsBodyComponent& bodyComp,
                                      const TransformComponent& transform)
    {
        auto& reg = world.registry();
        using namespace DirectX;
        const MeshComponent* meshComp = nullptr;
        std::string meshName;
        XMMATRIX meshLocalToRoot = XMMatrixIdentity();
        if (!find_mesh_source_in_subtree(world, entity, meshComp, meshName, meshLocalToRoot))
            return {};

        const std::filesystem::path modelPath = resolve_asset_load_path(meshComp->assetPath);
        if (modelPath.empty())
            return {};

        auto modelHandle = assetManager.load_model(modelPath);
        if (!modelHandle)
            return {};

        if (bodyComp.shapeType == PhysicsColliderShapeType::Mesh)
        {
            const MeshData* meshData = find_matching_mesh(*modelHandle, meshName);
            if (auto shape = meshData ? create_mesh_shape_from_mesh_data(*meshData, meshLocalToRoot) : JPH::ShapeRefC{}; shape)
                return shape;
        }
        else if (bodyComp.shapeType == PhysicsColliderShapeType::ConvexHull)
        {
            const MeshData* meshData = find_matching_mesh(*modelHandle, meshName);
            if (auto shape = meshData ? create_convex_hull_shape_from_mesh_data(*meshData, meshLocalToRoot) : JPH::ShapeRefC{}; shape)
                return shape;
        }
        else if (bodyComp.shapeType == PhysicsColliderShapeType::StaticCompound ||
                 bodyComp.shapeType == PhysicsColliderShapeType::MutableCompound)
        {
            if (modelHandle->meshes.empty())
                return {};

            if (bodyComp.shapeType == PhysicsColliderShapeType::StaticCompound)
            {
                JPH::StaticCompoundShapeSettings settings;
                for (const auto& mesh : modelHandle->meshes)
                {
                    auto childShape = create_convex_hull_shape_from_mesh_data(mesh, XMMatrixIdentity());
                    if (childShape)
                        settings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), childShape.GetPtr());
                }
                if (!settings.mSubShapes.empty())
                    return create_shape_from_result(settings.Create());
            }
            else
            {
                JPH::MutableCompoundShapeSettings settings;
                for (const auto& mesh : modelHandle->meshes)
                {
                    auto childShape = create_convex_hull_shape_from_mesh_data(mesh, XMMatrixIdentity());
                    if (childShape)
                        settings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), childShape.GetPtr());
                }
                if (!settings.mSubShapes.empty())
                    return create_shape_from_result(settings.Create());
            }
        }

        return create_primitive_shape(bodyComp, transform);
    }

    JPH::ShapeRefC create_collision_shape(World& world,
                                          entt::entity entity,
                                          const PhysicsBodyComponent& bodyComp,
                                          const TransformComponent& transform)
    {
        if (bodyComp.shapeType == PhysicsColliderShapeType::Mesh ||
            bodyComp.shapeType == PhysicsColliderShapeType::ConvexHull ||
            bodyComp.shapeType == PhysicsColliderShapeType::StaticCompound ||
            bodyComp.shapeType == PhysicsColliderShapeType::MutableCompound)
        {
            if (auto shape = create_model_shape(world, entity, bodyComp, transform); shape)
                return shape;
        }

        return create_primitive_shape(bodyComp, transform);
    }

    static JPH::EMotionType effective_motion_type(const PhysicsBodyComponent& bodyComp) noexcept
    {
        if (bodyComp.shapeType == PhysicsColliderShapeType::StaticCompound)
            return JPH::EMotionType::Static;

        return to_motion_type(bodyComp.motionType);
    }

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

    void recreate_body(World& world, PhysicsBodyComponent& bodyComp, const TransformComponent& transform, entt::entity entity)
    {
        const auto key = entity_key(entity);
        if (auto bodyIt = bodies.find(key); bodyIt != bodies.end())
        {
            destroy_body(bodyIt->second);
            bodies.erase(bodyIt);
        }

        JPH::ShapeRefC shape = create_collision_shape(world, entity, bodyComp, transform);
        if (!shape)
            return;

        const JPH::EMotionType motionType = effective_motion_type(bodyComp);
        JPH::BodyCreationSettings settings(
            shape,
            to_body_position(transform, bodyComp),
            to_jolt_quat(transform.rotation),
            motionType,
            motionType == JPH::EMotionType::Static ? Layers::NON_MOVING : Layers::MOVING);

        if (bodyComp.shapeType == PhysicsColliderShapeType::Mesh && motionType != JPH::EMotionType::Static)
        {
            // Mesh shapes don't provide stable volume-based mass properties for dynamic simulation.
            // Provide an explicit mass and let Jolt compute inertia from shape density.
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = 1.0f;
            settings.mEnhancedInternalEdgeRemoval = true;
        }

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
                recreate_body(world, bodyComp, transform, entity);

            bodyIt = bodies.find(entity_key(entity));
            if (bodyIt == bodies.end())
                continue;

            const JPH::EMotionType motionType = effective_motion_type(bodyComp);
            if (motionType == JPH::EMotionType::Static || motionType == JPH::EMotionType::Kinematic)
            {
                JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
                bodyInterface.SetPositionAndRotationWhenChanged(
                    bodyIt->second,
                    to_body_position(transform, bodyComp),
                    to_jolt_quat(transform.rotation),
                    JPH::EActivation::DontActivate);
            }
            else if (!enabled)
            {
                // Keep dynamic bodies in editor-authored pose while simulation is disabled.
                JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
                bodyInterface.SetPositionAndRotationWhenChanged(
                    bodyIt->second,
                    to_body_position(transform, bodyComp),
                    to_jolt_quat(transform.rotation),
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

void PhysicsWorld::set_asset_root(const std::string& rootPath)
{
    if (!m_impl)
        return;

    m_impl->assetRoot = rootPath.empty() ? std::filesystem::path{} : std::filesystem::path(rootPath);
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
        if (!bodyComp.enabled || m_impl->effective_motion_type(bodyComp) != JPH::EMotionType::Dynamic)
            continue;

        auto bodyIt = m_impl->bodies.find(entity_key(entity));
        if (bodyIt == m_impl->bodies.end())
            continue;

        auto& transform = view.get<TransformComponent>(entity);
        const JPH::RVec3 position = bodyInterface.GetPosition(bodyIt->second);
        const JPH::Quat rotation = bodyInterface.GetRotation(bodyIt->second);

        transform.rotation = {rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW()};
        transform.position = to_entity_position(position, transform.rotation, bodyComp.colliderOffset);
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
