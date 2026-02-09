#pragma once
#include "../../Core/Public/Expected.hpp"
#include "../Public/ModelData.hpp"

#include <memory>
#include <string_view>

/// Loads a complete model (OBJ + MTL) into ModelData.
/// Splits geometry by material, extracts material properties and texture paths,
/// computes tangent vectors per sub-mesh.
///
/// Conforms to the EnTT resource_cache loader concept:
///   operator()(args...) -> shared_ptr<ModelData>
struct ModelLoader
{
    using result_type = std::shared_ptr<ModelData>;

    /// Load a model from the given OBJ file path.
    /// Returns nullptr on failure (EnTT cache convention).
    result_type operator()(std::string_view path) const;

    /// Load with full error reporting.
    static Result<std::shared_ptr<ModelData>> parse_model(std::string_view path);
};
