#include "MeshLoader.hpp"
#include "../../Rendering/Public/Mesh.hpp"
#include "../Public/MeshData.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include <MeshAsset_generated.h>
#include <flatbuffers/flatbuffers.h>
#include <fmt/core.h>
#include <rapidobj/rapidobj.hpp>

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
        size_t h4 = hash<float>()(vertex.tangent.x) ^ (hash<float>()(vertex.tangent.y) << 1) ^
                    (hash<float>()(vertex.tangent.z) << 2) ^ (hash<float>()(vertex.tangent.w) << 3);
        return h1 ^ (h2 << 1) ^ (h3 << 1) ^ (h4 << 2);
    }
};
} // namespace std

std::filesystem::path MeshLoader::get_cache_path(std::filesystem::path& sourcePath)
{
    sourcePath.replace_extension(".noc_mesh");
    return sourcePath;
}

MeshLoader::result_type MeshLoader::operator()(const std::filesystem::path& path) const
{
    std::filesystem::path temPath = path;
    const std::filesystem::path cachePath = get_cache_path(temPath);

    if (std::filesystem::exists(cachePath) && std::filesystem::exists(path))
    {
        auto sourceTime = std::filesystem::last_write_time(path);
        auto cacheTime = std::filesystem::last_write_time(cachePath);

        if (cacheTime >= sourceTime)
        {
            auto cached = read_cache(cachePath.string());
            if (cached)
            {
                fmt::print("Loaded mesh from cache: {}\n", cachePath.generic_string());
                return cached.value();
            }
            fmt::print("Cache read failed, re-parsing: {}\n", path.string());
        }
    }

    auto parsed = parse_obj(path);
    if (!parsed)
    {
        fmt::print("ERROR: Failed to parse mesh: {}\n", parsed.error().message);
        return nullptr;
    }

    auto writeResult = write_cache(*parsed.value(), cachePath.string());
    if (!writeResult)
    {
        fmt::print("WARNING: Failed to write mesh cache: {}\n", writeResult.error().message);
    }
    else
    {
        fmt::print("Wrote mesh cache: {}\n", cachePath.generic_string());
    }

    return parsed.value();
}

Result<std::shared_ptr<MeshData>> MeshLoader::parse_obj(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
        return make_error(fmt::format("File not found: {}", path.string()), ErrorCode::AssetFileNotFound);

    rapidobj::Result result =
        rapidobj::ParseFile(path, rapidobj::MaterialLibrary::Default(rapidobj::Load::Optional));
    if (result.error)
        return make_error(result.error.code.message(), ErrorCode::AssetParsingFailed);

    std::vector<Vertex> vertices{};
    std::vector<std::uint32_t> indices{};
    std::unordered_map<Vertex, std::uint32_t> uniqueVertices{};

    const auto& attrib = result.attributes;
    const auto numPositions = attrib.positions.size() / 3;
    const auto numTexCoords = attrib.texcoords.size() / 2;
    const auto numNormals = attrib.normals.size() / 3;

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

    auto addVertex = [&](const Vertex& vertex) {
        if (uniqueVertices.count(vertex) == 0)
        {
            uniqueVertices[vertex] = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(vertex);
        }
        indices.push_back(uniqueVertices[vertex]);
    };

    for (const auto& shape : result.shapes)
    {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
        {
            const auto numFaceVerts = static_cast<size_t>(shape.mesh.num_face_vertices[f]);

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
                    addVertex(v0);
                    addVertex(v1);
                    addVertex(v2);
                }
            }

            indexOffset += numFaceVerts;
        }
    }

    if (vertices.empty())
        return make_error("Model has no valid vertices", ErrorCode::AssetInvalidData);

    auto mesh = std::make_shared<MeshData>();
    mesh->name = path.stem().string();
    mesh->sourcePath = path;
    mesh->vertices = std::move(vertices);
    mesh->indices = std::move(indices);
    mesh->compute_bounds();

    {
        auto& verts = mesh->vertices;
        const auto& idxs = mesh->indices;
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

            float dx1 = v1.pos.x - v0.pos.x;
            float dy1 = v1.pos.y - v0.pos.y;
            float dz1 = v1.pos.z - v0.pos.z;
            float dx2 = v2.pos.x - v0.pos.x;
            float dy2 = v2.pos.y - v0.pos.y;
            float dz2 = v2.pos.z - v0.pos.z;

            float du1 = v1.texCoord.x - v0.texCoord.x;
            float dv1 = v1.texCoord.y - v0.texCoord.y;
            float du2 = v2.texCoord.x - v0.texCoord.x;
            float dv2 = v2.texCoord.y - v0.texCoord.y;

            float det = du1 * dv2 - du2 * dv1;
            if (std::abs(det) < 1e-8f)
                continue;

            float invDet = 1.0f / det;

            float tx = (dv2 * dx1 - dv1 * dx2) * invDet;
            float ty = (dv2 * dy1 - dv1 * dy2) * invDet;
            float tz = (dv2 * dz1 - dv1 * dz2) * invDet;

            float bx = (du1 * dx2 - du2 * dx1) * invDet;
            float by = (du1 * dy2 - du2 * dy1) * invDet;
            float bz = (du1 * dz2 - du2 * dz1) * invDet;

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

    return mesh;
}

Result<> MeshLoader::write_cache(const MeshData& mesh, const std::filesystem::path& cachePath)
{
    namespace fb = NatureOfCraft::Assets;

    flatbuffers::FlatBufferBuilder builder(1024);

    std::vector<fb::VertexData> fbVertices;
    fbVertices.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices)
    {
        fbVertices.emplace_back(fb::Vec3(v.pos.x, v.pos.y, v.pos.z), fb::Vec3(v.normal.x, v.normal.y, v.normal.z),
                                fb::Vec2(v.texCoord.x, v.texCoord.y),
                                fb::Vec4(v.tangent.x, v.tangent.y, v.tangent.z, v.tangent.w));
    }

    fb::Vec3 bMin(mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z);
    fb::Vec3 bMax(mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z);

    const std::string meshSourcePath = mesh.sourcePath.generic_string();
    auto meshAsset = fb::CreateMeshAssetDirect(builder, mesh.name.c_str(), meshSourcePath.c_str(), &fbVertices,
                                               &mesh.indices, &bMin, &bMax);

    fb::FinishMeshAssetBuffer(builder, meshAsset);

    std::ofstream file(cachePath, std::ios::binary);
    if (!file.is_open())
        return make_error(fmt::format("Failed to open cache file for writing: {}", cachePath.string()),
                          ErrorCode::AssetCacheWriteFailed);

    file.write(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize());
    if (!file.good())
        return make_error(fmt::format("Failed to write cache file: {}", cachePath.string()), ErrorCode::AssetCacheWriteFailed);

    return {};
}

Result<std::shared_ptr<MeshData>> MeshLoader::read_cache(const std::filesystem::path& cachePath)
{
    namespace fb = NatureOfCraft::Assets;

    std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return make_error(fmt::format("Failed to open cache file: {}", cachePath.string()), ErrorCode::AssetCacheReadFailed);

    auto fileSize = file.tellg();
    if (fileSize <= 0)
        return make_error(fmt::format("Cache file is empty: {}", cachePath.string()), ErrorCode::AssetCacheReadFailed);

    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    flatbuffers::Verifier verifier(buffer.data(), buffer.size());
    if (!fb::VerifyMeshAssetBuffer(verifier))
        return make_error(fmt::format("Cache file verification failed: {}", cachePath.string()),
                          ErrorCode::AssetCacheReadFailed);

    const auto* meshAsset = fb::GetMeshAsset(buffer.data());
    if (!meshAsset)
        return make_error(fmt::format("Failed to deserialize cache file: {}", cachePath.string()),
                          ErrorCode::AssetCacheReadFailed);

    auto mesh = std::make_shared<MeshData>();

    if (meshAsset->name())
        mesh->name = meshAsset->name()->str();
    if (meshAsset->source_path())
        mesh->sourcePath = meshAsset->source_path()->str();

    if (meshAsset->vertices())
    {
        mesh->vertices.reserve(meshAsset->vertices()->size());
        for (const auto* v : *meshAsset->vertices())
        {
            Vertex vertex{};
            vertex.pos = {v->position().x(), v->position().y(), v->position().z()};
            vertex.normal = {v->normal().x(), v->normal().y(), v->normal().z()};
            vertex.texCoord = {v->tex_coord().x(), v->tex_coord().y()};
            vertex.tangent = {v->tangent().x(), v->tangent().y(), v->tangent().z(), v->tangent().w()};
            mesh->vertices.push_back(vertex);
        }
    }

    if (meshAsset->indices())
    {
        const auto* idxVec = meshAsset->indices();
        mesh->indices.assign(idxVec->begin(), idxVec->end());
    }

    if (meshAsset->bounds_min())
    {
        mesh->boundsMin = {meshAsset->bounds_min()->x(), meshAsset->bounds_min()->y(), meshAsset->bounds_min()->z()};
    }
    if (meshAsset->bounds_max())
    {
        mesh->boundsMax = {meshAsset->bounds_max()->x(), meshAsset->bounds_max()->y(), meshAsset->bounds_max()->z()};
    }

    return mesh;
}
