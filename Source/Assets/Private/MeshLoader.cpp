#include "MeshLoader.hpp"
#include "../../Rendering/Public/Mesh.hpp"
#include "../Public/MeshData.hpp"

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
        return h1 ^ (h2 << 1) ^ (h3 << 1);
    }
};
} // namespace std

std::string MeshLoader::get_cache_path(std::string_view sourcePath)
{
    std::filesystem::path p(sourcePath);
    p.replace_extension(".noc_mesh");
    return p.string();
}

MeshLoader::result_type MeshLoader::operator()(std::string_view path) const
{
    std::string cachePath = get_cache_path(path);

    // Check if the binary cache is newer than the source file
    if (std::filesystem::exists(cachePath) && std::filesystem::exists(path))
    {
        auto sourceTime = std::filesystem::last_write_time(std::filesystem::path(path));
        auto cacheTime = std::filesystem::last_write_time(std::filesystem::path(cachePath));

        if (cacheTime >= sourceTime)
        {
            auto cached = read_cache(cachePath);
            if (cached)
            {
                fmt::print("Loaded mesh from cache: {}\n", cachePath);
                return cached.value();
            }
            // Cache read failed, fall through to re-parse
            fmt::print("Cache read failed, re-parsing: {}\n", path);
        }
    }

    // Parse OBJ
    auto parsed = parse_obj(path);
    if (!parsed)
    {
        fmt::print("ERROR: Failed to parse mesh: {}\n", parsed.error().message);
        return nullptr;
    }

    // Write cache for next time
    auto writeResult = write_cache(*parsed.value(), cachePath);
    if (!writeResult)
    {
        fmt::print("WARNING: Failed to write mesh cache: {}\n", writeResult.error().message);
    }
    else
    {
        fmt::print("Wrote mesh cache: {}\n", cachePath);
    }

    return parsed.value();
}

Result<std::shared_ptr<MeshData>> MeshLoader::parse_obj(std::string_view path)
{
    std::filesystem::path filePath(path);

    if (!std::filesystem::exists(filePath))
        return make_error(fmt::format("File not found: {}", path), ErrorCode::AssetFileNotFound);

    rapidobj::Result result =
        rapidobj::ParseFile(filePath, rapidobj::MaterialLibrary::Default(rapidobj::Load::Optional));
    if (result.error)
        return make_error(result.error.code.message(), ErrorCode::AssetParsingFailed);

    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};
    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

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
            uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
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
    mesh->name = filePath.stem().string();
    mesh->sourcePath = std::string(path);
    mesh->vertices = std::move(vertices);
    mesh->indices = std::move(indices);
    mesh->compute_bounds();

    return mesh;
}

Result<> MeshLoader::write_cache(const MeshData& mesh, std::string_view cachePath)
{
    namespace fb = NatureOfCraft::Assets;

    flatbuffers::FlatBufferBuilder builder(1024);

    // Convert vertices to FlatBuffer VertexData structs
    std::vector<fb::VertexData> fbVertices;
    fbVertices.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices)
    {
        fbVertices.emplace_back(fb::Vec3(v.pos.x, v.pos.y, v.pos.z), fb::Vec3(v.normal.x, v.normal.y, v.normal.z),
                                fb::Vec2(v.texCoord.x, v.texCoord.y));
    }

    fb::Vec3 bMin(mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z);
    fb::Vec3 bMax(mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z);

    auto meshAsset = fb::CreateMeshAssetDirect(builder, mesh.name.c_str(), mesh.sourcePath.c_str(), &fbVertices,
                                               &mesh.indices, &bMin, &bMax);

    fb::FinishMeshAssetBuffer(builder, meshAsset);

    std::ofstream file(std::string(cachePath), std::ios::binary);
    if (!file.is_open())
        return make_error(fmt::format("Failed to open cache file for writing: {}", cachePath),
                          ErrorCode::AssetCacheWriteFailed);

    file.write(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize());
    if (!file.good())
        return make_error(fmt::format("Failed to write cache file: {}", cachePath), ErrorCode::AssetCacheWriteFailed);

    return {};
}

Result<std::shared_ptr<MeshData>> MeshLoader::read_cache(std::string_view cachePath)
{
    namespace fb = NatureOfCraft::Assets;

    std::ifstream file(std::string(cachePath), std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return make_error(fmt::format("Failed to open cache file: {}", cachePath), ErrorCode::AssetCacheReadFailed);

    auto fileSize = file.tellg();
    if (fileSize <= 0)
        return make_error(fmt::format("Cache file is empty: {}", cachePath), ErrorCode::AssetCacheReadFailed);

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    // Verify the buffer
    flatbuffers::Verifier verifier(buffer.data(), buffer.size());
    if (!fb::VerifyMeshAssetBuffer(verifier))
        return make_error(fmt::format("Cache file verification failed: {}", cachePath),
                          ErrorCode::AssetCacheReadFailed);

    const auto* meshAsset = fb::GetMeshAsset(buffer.data());
    if (!meshAsset)
        return make_error(fmt::format("Failed to deserialize cache file: {}", cachePath),
                          ErrorCode::AssetCacheReadFailed);

    auto mesh = std::make_shared<MeshData>();

    if (meshAsset->name())
        mesh->name = meshAsset->name()->str();
    if (meshAsset->source_path())
        mesh->sourcePath = meshAsset->source_path()->str();

    // Convert FlatBuffer vertices back to Vertex structs
    if (meshAsset->vertices())
    {
        mesh->vertices.reserve(meshAsset->vertices()->size());
        for (const auto* v : *meshAsset->vertices())
        {
            Vertex vertex{};
            vertex.pos = {v->position().x(), v->position().y(), v->position().z()};
            vertex.normal = {v->normal().x(), v->normal().y(), v->normal().z()};
            vertex.texCoord = {v->tex_coord().x(), v->tex_coord().y()};
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
