#pragma once
#include "../../Core/Public/Core.hpp"
#include "MaterialData.hpp"
#include "MeshData.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

NOC_SUPPRESS_DLL_WARNINGS

/// CPU-side model data: a collection of meshes with associated materials.
/// Produced by loading an OBJ+MTL file. Each mesh corresponds to one material group.
/// meshMaterialIndices[i] is the index into 'materials' for meshes[i].
struct NOC_EXPORT ModelData
{
    /// Human-readable model identifier, usually derived from source filename.
    std::string name{};
    /// Original model source path used to build this asset.
    std::filesystem::path sourcePath{};

    /// Sub-meshes generated from source geometry, typically grouped by material.
    std::vector<MeshData> meshes{};
    /// Material entries extracted from source data.
    std::vector<MaterialData> materials{};
    /// Mapping where meshes[i] uses materials[meshMaterialIndices[i]].
    std::vector<std::int32_t> meshMaterialIndices{};
};

NOC_RESTORE_DLL_WARNINGS
