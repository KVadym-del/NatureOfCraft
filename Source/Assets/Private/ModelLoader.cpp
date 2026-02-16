#include "ModelLoader.hpp"
#include "../../Rendering/Public/Mesh.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unordered_map>

#include <ModelAsset_generated.h>
#include <flatbuffers/flatbuffers.h>
#include <fmt/core.h>
#include <rapidobj/rapidobj.hpp>
#include <taskflow/taskflow.hpp>

#include <DirectXMath.h>
using namespace DirectX;

static tf::Executor& model_loader_executor()
{
    const std::uint32_t workerCount = std::thread::hardware_concurrency();
    static tf::Executor executor(workerCount > 0 ? workerCount : 1);
    return executor;
}

namespace std
{
template <> struct hash<Vertex>
{
    size_t operator()(Vertex const& vertex) const
    {
        size_t h1 =
            hash<float>()(vertex.pos.x) ^ (hash<float>()(vertex.pos.y) << 1) ^ (hash<float>()(vertex.pos.z) << 2);
        size_t h2 = hash<float>()(vertex.normal.x) ^ (hash<float>()(vertex.normal.y) << 1) ^
                    (hash<float>()(vertex.normal.z) << 2);
        size_t h3 = hash<float>()(vertex.texCoord.x) ^ (hash<float>()(vertex.texCoord.y) << 1);
        return h1 ^ (h2 << 1) ^ (h3 << 1);
    }
};
} // namespace std

// Compute tangent vectors for a MeshData in-place.
// Accumulates per-triangle tangent/bitangent, then Gram-Schmidt orthogonalizes against normal.
static void compute_tangents(MeshData& mesh)
{
    auto& verts = mesh.vertices;
    const auto& idxs = mesh.indices;
    const size_t vertCount = verts.size();

    std::vector<XMFLOAT3> tanAccum(vertCount, {0.0f, 0.0f, 0.0f});
    std::vector<XMFLOAT3> bitanAccum(vertCount, {0.0f, 0.0f, 0.0f});

    for (size_t i = 0; i + 2 < idxs.size(); i += 3)
    {
        std::uint32_t i0 = idxs[i + 0];
        std::uint32_t i1 = idxs[i + 1];
        std::uint32_t i2 = idxs[i + 2];
        const auto& v0 = verts[i0];
        const auto& v1 = verts[i1];
        const auto& v2 = verts[i2];

        float dx1 = v1.pos.x - v0.pos.x, dy1 = v1.pos.y - v0.pos.y, dz1 = v1.pos.z - v0.pos.z;
        float dx2 = v2.pos.x - v0.pos.x, dy2 = v2.pos.y - v0.pos.y, dz2 = v2.pos.z - v0.pos.z;
        float du1 = v1.texCoord.x - v0.texCoord.x, dv1 = v1.texCoord.y - v0.texCoord.y;
        float du2 = v2.texCoord.x - v0.texCoord.x, dv2 = v2.texCoord.y - v0.texCoord.y;

        float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-8f)
            continue;

        float inv = 1.0f / det;
        float tx = (dv2 * dx1 - dv1 * dx2) * inv;
        float ty = (dv2 * dy1 - dv1 * dy2) * inv;
        float tz = (dv2 * dz1 - dv1 * dz2) * inv;
        float bx = (du1 * dx2 - du2 * dx1) * inv;
        float by = (du1 * dy2 - du2 * dy1) * inv;
        float bz = (du1 * dz2 - du2 * dz1) * inv;

        for (std::uint32_t idx : {i0, i1, i2})
        {
            tanAccum[idx].x += tx;
            tanAccum[idx].y += ty;
            tanAccum[idx].z += tz;
            bitanAccum[idx].x += bx;
            bitanAccum[idx].y += by;
            bitanAccum[idx].z += bz;
        }
    }

    for (size_t i = 0; i < vertCount; ++i)
    {
        XMVECTOR n = XMLoadFloat3(&verts[i].normal);
        XMVECTOR t = XMLoadFloat3(&tanAccum[i]);
        XMVECTOR b = XMLoadFloat3(&bitanAccum[i]);

        XMVECTOR nDotT = XMVector3Dot(n, t);
        XMVECTOR tOrtho = XMVector3Normalize(XMVectorSubtract(t, XMVectorMultiply(n, nDotT)));

        if (XMVectorGetX(XMVector3Length(t)) < 1e-8f)
        {
            XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            if (std::abs(XMVectorGetX(XMVector3Dot(n, up))) > 0.99f)
                up = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
            tOrtho = XMVector3Normalize(XMVector3Cross(n, up));
        }

        float handedness = (XMVectorGetX(XMVector3Dot(XMVector3Cross(n, tOrtho), b)) < 0.0f) ? -1.0f : 1.0f;

        XMFLOAT3 tResult;
        XMStoreFloat3(&tResult, tOrtho);
        verts[i].tangent = {tResult.x, tResult.y, tResult.z, handedness};
    }
}

ModelLoader::result_type ModelLoader::operator()(const std::filesystem::path& path) const
{
    if (path.extension() == ".noc_model")
    {
        auto cached = read_cache(path);
        if (cached)
        {
            fmt::print("Loaded model from cache: {}\n", path.string());
            return std::move(cached.value());
        }
        fmt::print(stderr, "ModelLoader: failed to read cache '{}': {}\n", path.string(), cached.error().message);
        return nullptr;
    }

    auto result = parse_model(path);
    if (!result)
    {
        fmt::print(stderr, "ModelLoader: failed to load '{}': {}\n", path.string(), result.error().message);
        return nullptr;
    }
    return std::move(result.value());
}

Result<std::shared_ptr<ModelData>> ModelLoader::parse_model(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
        return make_error(fmt::format("Model file not found: {}", path.string()), ErrorCode::AssetFileNotFound);

    rapidobj::Result result =
        rapidobj::ParseFile(path, rapidobj::MaterialLibrary::Default(rapidobj::Load::Optional));
    if (result.error)
        return make_error(result.error.code.message(), ErrorCode::AssetParsingFailed);

    const auto& attrib = result.attributes;
    const auto numPositions = attrib.positions.size() / 3;
    const auto numTexCoords = attrib.texcoords.size() / 2;
    const auto numNormals = attrib.normals.size() / 3;

    const std::filesystem::path objDir = path.parent_path();

    std::vector<MaterialData> materials;
    for (const auto& mat : result.materials)
    {
        MaterialData matData;
        matData.name = mat.name;
        matData.albedoColor = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1.0f};

        if (!mat.diffuse_texname.empty())
            matData.albedoTexturePath = objDir / mat.diffuse_texname;

        if (!mat.bump_texname.empty())
            matData.normalTexturePath = objDir / mat.bump_texname;

        materials.push_back(std::move(matData));
    }

    if (materials.empty())
    {
        MaterialData defaultMat;
        defaultMat.name = "default";
        materials.push_back(std::move(defaultMat));
    }

    auto makeVertex = [&](const rapidobj::Index& index) -> Vertex {
        Vertex vertex{};
        vertex.pos = {attrib.positions[3 * index.position_index + 0], attrib.positions[3 * index.position_index + 1],
                      attrib.positions[3 * index.position_index + 2]};

        if (index.texcoord_index >= 0 && static_cast<size_t>(index.texcoord_index) < numTexCoords)
        {
            vertex.texCoord = {attrib.texcoords[2 * index.texcoord_index + 0],
                               1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};
        }

        if (index.normal_index >= 0 && static_cast<size_t>(index.normal_index) < numNormals)
        {
            vertex.normal = {attrib.normals[3 * index.normal_index + 0], attrib.normals[3 * index.normal_index + 1],
                             attrib.normals[3 * index.normal_index + 2]};
        }
        else
        {
            vertex.normal = {0.0f, 1.0f, 0.0f};
        }
        return vertex;
    };

    struct MeshBuild
    {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        std::unordered_map<Vertex, std::uint32_t> uniqueVertices;

        void addVertex(const Vertex& v)
        {
            auto [it, inserted] = uniqueVertices.emplace(v, static_cast<std::uint32_t>(vertices.size()));
            if (inserted)
                vertices.push_back(v);
            indices.push_back(it->second);
        }
    };

    const size_t matCount = materials.size();
    std::vector<MeshBuild> meshBuilds(matCount);

    if (result.shapes.size() <= 1)
    {
        if (!result.shapes.empty())
        {
            const auto& shape = result.shapes[0];
            size_t indexOffset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
            {
                const auto numFaceVerts = static_cast<size_t>(shape.mesh.num_face_vertices[f]);

                std::int32_t matIdx = 0;
                if (f < shape.mesh.material_ids.size())
                {
                    const std::int32_t rawMat = static_cast<std::int32_t>(shape.mesh.material_ids[f]);
                    if (rawMat >= 0 && static_cast<size_t>(rawMat) < matCount)
                        matIdx = rawMat;
                }

                bool faceValid = numFaceVerts >= 3;
                if (faceValid)
                {
                    for (size_t v = 0; v < numFaceVerts; ++v)
                    {
                        const auto& idx = shape.mesh.indices[indexOffset + v];
                        if (idx.position_index < 0 || static_cast<size_t>(idx.position_index) >= numPositions)
                        {
                            faceValid = false;
                            break;
                        }
                    }
                }

                if (faceValid)
                {
                    const Vertex v0 = makeVertex(shape.mesh.indices[indexOffset]);
                    for (size_t v = 1; v + 1 < numFaceVerts; ++v)
                    {
                        const Vertex v1 = makeVertex(shape.mesh.indices[indexOffset + v]);
                        const Vertex v2 = makeVertex(shape.mesh.indices[indexOffset + v + 1]);
                        meshBuilds[static_cast<size_t>(matIdx)].addVertex(v0);
                        meshBuilds[static_cast<size_t>(matIdx)].addVertex(v1);
                        meshBuilds[static_cast<size_t>(matIdx)].addVertex(v2);
                    }
                }

                indexOffset += numFaceVerts;
            }
        }
    }
    else
    {
        using MaterialMeshBuildMap = std::unordered_map<std::int32_t, MeshBuild>;
        std::vector<MaterialMeshBuildMap> perShapeBuilds(result.shapes.size());

        auto build_shape_geometry = [&](size_t shapeIndex) {
            const auto& shape = result.shapes[shapeIndex];
            auto& shapeBuilds = perShapeBuilds[shapeIndex];

            size_t indexOffset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
            {
                const auto numFaceVerts = static_cast<size_t>(shape.mesh.num_face_vertices[f]);

                std::int32_t matIdx = 0;
                if (f < shape.mesh.material_ids.size())
                {
                    const std::int32_t rawMat = static_cast<std::int32_t>(shape.mesh.material_ids[f]);
                    if (rawMat >= 0 && static_cast<size_t>(rawMat) < matCount)
                        matIdx = rawMat;
                }

                bool faceValid = numFaceVerts >= 3;
                if (faceValid)
                {
                    for (size_t v = 0; v < numFaceVerts; ++v)
                    {
                        const auto& idx = shape.mesh.indices[indexOffset + v];
                        if (idx.position_index < 0 || static_cast<size_t>(idx.position_index) >= numPositions)
                        {
                            faceValid = false;
                            break;
                        }
                    }
                }

                if (faceValid)
                {
                    auto& meshBuild = shapeBuilds[matIdx];
                    const Vertex v0 = makeVertex(shape.mesh.indices[indexOffset]);
                    for (size_t v = 1; v + 1 < numFaceVerts; ++v)
                    {
                        const Vertex v1 = makeVertex(shape.mesh.indices[indexOffset + v]);
                        const Vertex v2 = makeVertex(shape.mesh.indices[indexOffset + v + 1]);
                        meshBuild.addVertex(v0);
                        meshBuild.addVertex(v1);
                        meshBuild.addVertex(v2);
                    }
                }

                indexOffset += numFaceVerts;
            }
        };

        tf::Taskflow taskflow;
        for (size_t shapeIndex = 0; shapeIndex < result.shapes.size(); ++shapeIndex)
        {
            taskflow.emplace([&, shapeIndex]() { build_shape_geometry(shapeIndex); });
        }
        model_loader_executor().run(taskflow).wait();

        for (auto& shapeBuilds : perShapeBuilds)
        {
            for (auto& [matIdx, localBuild] : shapeBuilds)
            {
                if (matIdx < 0 || static_cast<size_t>(matIdx) >= matCount)
                    continue;

                auto& mergedBuild = meshBuilds[static_cast<size_t>(matIdx)];
                for (std::uint32_t localIndex : localBuild.indices)
                {
                    if (localIndex >= localBuild.vertices.size())
                        continue;
                    mergedBuild.addVertex(localBuild.vertices[localIndex]);
                }
            }
        }
    }

    auto model = std::make_shared<ModelData>();
    model->name = path.stem().string();
    model->sourcePath = path;
    model->materials = std::move(materials);

    std::vector<size_t> activeMaterialIndices;
    activeMaterialIndices.reserve(matCount);
    for (size_t i = 0; i < matCount; ++i)
    {
        if (!meshBuilds[i].vertices.empty())
            activeMaterialIndices.push_back(i);
    }

    std::vector<MeshData> builtMeshes(activeMaterialIndices.size());
    std::vector<std::int32_t> builtMaterialIndices(activeMaterialIndices.size(), 0);

    auto build_submesh = [&](size_t outputIndex) {
        const size_t materialIndex = activeMaterialIndices[outputIndex];
        MeshData meshData;
        meshData.name = fmt::format("{}_{}", model->name, model->materials[materialIndex].name);
        meshData.sourcePath = model->sourcePath;
        meshData.vertices = std::move(meshBuilds[materialIndex].vertices);
        meshData.indices = std::move(meshBuilds[materialIndex].indices);
        meshData.compute_bounds();
        compute_tangents(meshData);
        builtMeshes[outputIndex] = std::move(meshData);
        builtMaterialIndices[outputIndex] = static_cast<std::int32_t>(materialIndex);
    };

    if (activeMaterialIndices.size() > 1)
    {
        tf::Taskflow taskflow;
        for (size_t outputIndex = 0; outputIndex < activeMaterialIndices.size(); ++outputIndex)
        {
            taskflow.emplace([&, outputIndex]() { build_submesh(outputIndex); });
        }
        model_loader_executor().run(taskflow).wait();
    }
    else if (!activeMaterialIndices.empty())
    {
        build_submesh(0);
    }

    model->meshes = std::move(builtMeshes);
    model->meshMaterialIndices = std::move(builtMaterialIndices);

    if (model->meshes.empty())
        return make_error("Model has no valid geometry", ErrorCode::AssetInvalidData);

    fmt::print("Loaded model '{}': {} meshes, {} materials\n", model->name, model->meshes.size(),
               model->materials.size());

    return model;
}

// ── Binary cache (.noc_model) ─────────────────────────────────────────

namespace fb = NatureOfCraft::Assets;

std::filesystem::path ModelLoader::get_cache_path(std::filesystem::path& sourcePath)
{
    sourcePath.replace_extension(".noc_model");
    return sourcePath;
}

Result<> ModelLoader::write_cache(const ModelData& model, const std::filesystem::path& cachePath)
{
    flatbuffers::FlatBufferBuilder fbb(4096);

    std::vector<flatbuffers::Offset<fb::SubMeshAsset>> meshOffsets;
    meshOffsets.reserve(model.meshes.size());

    for (const auto& mesh : model.meshes)
    {
        std::vector<fb::MVertexData> fbVerts;
        fbVerts.reserve(mesh.vertices.size());
        for (const auto& v : mesh.vertices)
        {
            fbVerts.emplace_back(fb::MVec3(v.pos.x, v.pos.y, v.pos.z), fb::MVec3(v.normal.x, v.normal.y, v.normal.z),
                                 fb::MVec2(v.texCoord.x, v.texCoord.y),
                                 fb::MVec4(v.tangent.x, v.tangent.y, v.tangent.z, v.tangent.w));
        }

        fb::MVec3 bMin(mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z);
        fb::MVec3 bMax(mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z);

        meshOffsets.push_back(fb::CreateSubMeshAssetDirect(fbb, mesh.name.c_str(), &fbVerts, &mesh.indices, &bMin, &bMax));
    }

    std::vector<flatbuffers::Offset<fb::MaterialEntry>> matOffsets;
    matOffsets.reserve(model.materials.size());

    for (const auto& mat : model.materials)
    {
        fb::MColor4 albedoCol(mat.albedoColor.x, mat.albedoColor.y, mat.albedoColor.z, mat.albedoColor.w);
        const std::string albedoPath = mat.albedoTexturePath.empty() ? std::string{} : mat.albedoTexturePath.generic_string();
        const std::string normalPath = mat.normalTexturePath.empty() ? std::string{} : mat.normalTexturePath.generic_string();
        const std::string roughnessPath =
            mat.roughnessTexturePath.empty() ? std::string{} : mat.roughnessTexturePath.generic_string();

        matOffsets.push_back(fb::CreateMaterialEntryDirect(
            fbb, mat.name.c_str(), &albedoCol, mat.roughness, mat.metallic,
            albedoPath.empty() ? nullptr : albedoPath.c_str(), normalPath.empty() ? nullptr : normalPath.c_str(),
            roughnessPath.empty() ? nullptr : roughnessPath.c_str()));
    }

    const std::string modelSourcePath = model.sourcePath.generic_string();
    auto asset = fb::CreateModelAssetDirect(fbb, model.name.c_str(), modelSourcePath.c_str(), &meshOffsets,
                                            &matOffsets, &model.meshMaterialIndices);

    fb::FinishModelAssetBuffer(fbb, asset);

    std::ofstream file(cachePath, std::ios::binary);
    if (!file.is_open())
        return make_error(fmt::format("Failed to open model cache for writing: {}", cachePath.string()),
                          ErrorCode::AssetCacheWriteFailed);

    file.write(reinterpret_cast<const char*>(fbb.GetBufferPointer()), static_cast<std::streamsize>(fbb.GetSize()));
    if (!file.good())
        return make_error(fmt::format("Failed to write model cache: {}", cachePath.string()), ErrorCode::AssetCacheWriteFailed);

    return {};
}

Result<std::shared_ptr<ModelData>> ModelLoader::read_cache(const std::filesystem::path& cachePath)
{
    std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return make_error(fmt::format("Failed to open model cache: {}", cachePath.string()), ErrorCode::AssetCacheReadFailed);

    auto fileSize = file.tellg();
    if (fileSize <= 0)
        return make_error(fmt::format("Model cache file is empty: {}", cachePath.string()),
                          ErrorCode::AssetCacheReadFailed);

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    flatbuffers::Verifier verifier(buffer.data(), buffer.size());
    if (!fb::VerifyModelAssetBuffer(verifier))
        return make_error(fmt::format("Model cache verification failed: {}", cachePath.string()),
                          ErrorCode::AssetCacheReadFailed);

    const auto* asset = fb::GetModelAsset(buffer.data());
    if (!asset)
        return make_error(fmt::format("Failed to deserialize model cache: {}", cachePath.string()),
                          ErrorCode::AssetCacheReadFailed);

    auto model = std::make_shared<ModelData>();

    if (asset->name())
        model->name = asset->name()->str();
    if (asset->source_path())
        model->sourcePath = asset->source_path()->str();

    if (asset->materials())
    {
        model->materials.reserve(asset->materials()->size());
        for (const auto* matEntry : *asset->materials())
        {
            MaterialData mat;
            if (matEntry->name())
                mat.name = matEntry->name()->str();

            if (matEntry->albedo_color())
            {
                const auto& c = *matEntry->albedo_color();
                mat.albedoColor = {c.r(), c.g(), c.b(), c.a()};
            }

            mat.roughness = matEntry->roughness();
            mat.metallic = matEntry->metallic();

            if (matEntry->albedo_texture_path())
                mat.albedoTexturePath = matEntry->albedo_texture_path()->str();
            if (matEntry->normal_texture_path())
                mat.normalTexturePath = matEntry->normal_texture_path()->str();
            if (matEntry->roughness_texture_path())
                mat.roughnessTexturePath = matEntry->roughness_texture_path()->str();

            model->materials.push_back(std::move(mat));
        }
    }

    if (asset->meshes())
    {
        model->meshes.reserve(asset->meshes()->size());
        for (const auto* subMesh : *asset->meshes())
        {
            MeshData meshData;
            if (subMesh->name())
                meshData.name = subMesh->name()->str();

            if (subMesh->vertices())
            {
                meshData.vertices.reserve(subMesh->vertices()->size());
                for (const auto* v : *subMesh->vertices())
                {
                    Vertex vert{};
                    vert.pos = {v->position().x(), v->position().y(), v->position().z()};
                    vert.normal = {v->normal().x(), v->normal().y(), v->normal().z()};
                    vert.texCoord = {v->tex_coord().x(), v->tex_coord().y()};
                    vert.tangent = {v->tangent().x(), v->tangent().y(), v->tangent().z(), v->tangent().w()};
                    meshData.vertices.push_back(vert);
                }
            }

            if (subMesh->indices())
            {
                const auto* idxVec = subMesh->indices();
                meshData.indices.assign(idxVec->begin(), idxVec->end());
            }

            if (subMesh->bounds_min())
                meshData.boundsMin = {subMesh->bounds_min()->x(), subMesh->bounds_min()->y(),
                                      subMesh->bounds_min()->z()};
            if (subMesh->bounds_max())
                meshData.boundsMax = {subMesh->bounds_max()->x(), subMesh->bounds_max()->y(),
                                      subMesh->bounds_max()->z()};

            model->meshes.push_back(std::move(meshData));
        }
    }

    if (asset->mesh_material_indices())
    {
        const auto* indices = asset->mesh_material_indices();
        model->meshMaterialIndices.assign(indices->begin(), indices->end());
    }

    fmt::print("Loaded model from cache '{}': {} meshes, {} materials\n", model->name, model->meshes.size(),
               model->materials.size());

    return model;
}
