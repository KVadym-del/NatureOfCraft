#pragma once
#include "../../Core/Public/Core.hpp"
#include "MaterialData.hpp"
#include "MeshData.hpp"

#include <cstdint>
#include <string>
#include <vector>

NOC_SUPPRESS_DLL_WARNINGS

/// CPU-side model data: a collection of meshes with associated materials.
/// Produced by loading an OBJ+MTL file. Each mesh corresponds to one material group.
/// meshMaterialIndices[i] is the index into 'materials' for meshes[i].
struct NOC_EXPORT ModelData
{
    std::string name{};
    std::string sourcePath{};

    std::vector<MeshData> meshes{};
    std::vector<MaterialData> materials{};
    std::vector<int32_t> meshMaterialIndices{}; // meshes[i] uses materials[meshMaterialIndices[i]]
};

NOC_RESTORE_DLL_WARNINGS
