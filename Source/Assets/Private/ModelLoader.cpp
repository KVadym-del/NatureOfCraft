#include "ModelLoader.hpp"
#include "../../Rendering/Public/Mesh.hpp"

#include <cmath>
#include <filesystem>
#include <unordered_map>

#include <fmt/core.h>
#include <rapidobj/rapidobj.hpp>

#include <DirectXMath.h>
using namespace DirectX;

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
        uint32_t i0 = idxs[i + 0];
        uint32_t i1 = idxs[i + 1];
        uint32_t i2 = idxs[i + 2];
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

        for (uint32_t idx : {i0, i1, i2})
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

ModelLoader::result_type ModelLoader::operator()(std::string_view path) const
{
    auto result = parse_model(path);
    if (!result)
    {
        fmt::print(stderr, "ModelLoader: failed to load '{}': {}\n", path, result.error().message);
        return nullptr;
    }
    return std::move(result.value());
}

Result<std::shared_ptr<ModelData>> ModelLoader::parse_model(std::string_view path)
{
    namespace fs = std::filesystem;

    fs::path filePath(path);
    if (!fs::exists(filePath))
        return make_error(fmt::format("Model file not found: {}", path), ErrorCode::AssetFileNotFound);

    // Parse OBJ with materials
    rapidobj::Result result =
        rapidobj::ParseFile(filePath, rapidobj::MaterialLibrary::Default(rapidobj::Load::Optional));
    if (result.error)
        return make_error(result.error.code.message(), ErrorCode::AssetParsingFailed);

    const auto& attrib = result.attributes;
    const auto numPositions = attrib.positions.size() / 3;
    const auto numTexCoords = attrib.texcoords.size() / 2;
    const auto numNormals = attrib.normals.size() / 3;

    // Directory containing the OBJ file (for resolving texture paths)
    fs::path objDir = filePath.parent_path();

    // Extract materials from rapidobj
    std::vector<MaterialData> materials;
    for (const auto& mat : result.materials)
    {
        MaterialData matData;
        matData.name = mat.name;
        matData.albedoColor = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1.0f};

        if (!mat.diffuse_texname.empty())
            matData.albedoTexturePath = (objDir / mat.diffuse_texname).string();

        if (!mat.bump_texname.empty())
            matData.normalTexturePath = (objDir / mat.bump_texname).string();

        materials.push_back(std::move(matData));
    }

    // If no materials were found, create a default one
    if (materials.empty())
    {
        MaterialData defaultMat;
        defaultMat.name = "default";
        materials.push_back(std::move(defaultMat));
    }

    // Build a vertex from rapidobj index
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

    // Collect geometry per material group.
    // Key: material index (-1 or >= 0), Value: (vertices, indices, unique vertex map)
    struct MeshBuild
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::unordered_map<Vertex, uint32_t> uniqueVertices;

        void addVertex(const Vertex& v)
        {
            auto [it, inserted] = uniqueVertices.emplace(v, static_cast<uint32_t>(vertices.size()));
            if (inserted)
                vertices.push_back(v);
            indices.push_back(it->second);
        }
    };

    // One MeshBuild per material
    const size_t matCount = materials.size();
    std::vector<MeshBuild> meshBuilds(matCount);

    for (const auto& shape : result.shapes)
    {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
        {
            const auto numFaceVerts = static_cast<size_t>(shape.mesh.num_face_vertices[f]);

            // Determine material for this face
            int matIdx = 0; // Default to first material
            if (f < shape.mesh.material_ids.size())
            {
                int rawMat = shape.mesh.material_ids[f];
                if (rawMat >= 0 && static_cast<size_t>(rawMat) < matCount)
                    matIdx = rawMat;
            }

            // Validate face
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
                // Fan triangulation
                const Vertex v0 = makeVertex(shape.mesh.indices[indexOffset]);
                for (size_t v = 1; v + 1 < numFaceVerts; ++v)
                {
                    const Vertex v1 = makeVertex(shape.mesh.indices[indexOffset + v]);
                    const Vertex v2 = makeVertex(shape.mesh.indices[indexOffset + v + 1]);
                    meshBuilds[matIdx].addVertex(v0);
                    meshBuilds[matIdx].addVertex(v1);
                    meshBuilds[matIdx].addVertex(v2);
                }
            }

            indexOffset += numFaceVerts;
        }
    }

    // Build ModelData from the per-material mesh builds
    auto model = std::make_shared<ModelData>();
    model->name = filePath.stem().string();
    model->sourcePath = std::string(path);
    model->materials = std::move(materials);

    for (size_t i = 0; i < matCount; ++i)
    {
        if (meshBuilds[i].vertices.empty())
            continue; // Skip empty material groups

        MeshData meshData;
        meshData.name = fmt::format("{}_{}", model->name, model->materials[i].name);
        meshData.sourcePath = std::string(path);
        meshData.vertices = std::move(meshBuilds[i].vertices);
        meshData.indices = std::move(meshBuilds[i].indices);
        meshData.compute_bounds();

        // Compute tangents for this sub-mesh
        compute_tangents(meshData);

        model->meshes.push_back(std::move(meshData));
        model->meshMaterialIndices.push_back(static_cast<int32_t>(i));
    }

    if (model->meshes.empty())
        return make_error("Model has no valid geometry", ErrorCode::AssetInvalidData);

    fmt::print("Loaded model '{}': {} meshes, {} materials\n", model->name, model->meshes.size(),
               model->materials.size());

    return model;
}
