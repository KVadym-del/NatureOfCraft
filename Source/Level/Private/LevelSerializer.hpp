#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Core/Public/Expected.hpp"
#include "../../ECS/Public/World.hpp"

#include <cstdint>
#include <string>
#include <vector>

NOC_SUPPRESS_DLL_WARNINGS

/// Serializes/deserializes a World (ECS registry) to/from a FlatBuffers binary (.noc_level).
class NOC_EXPORT LevelSerializer
{
  public:
    /// Serializes the given World into a FlatBuffers binary buffer.
    /// Returns the raw bytes suitable for writing to a file.
    static Result<std::vector<uint8_t>> serialize(const World& world, const std::string& levelName);

    /// Deserializes a FlatBuffers binary buffer into a World.
    /// The caller provides a reference to an empty World that will be populated.
    static Result<> deserialize(const std::vector<uint8_t>& buffer, World& world);

    /// Convenience: serialize directly to a file.
    static Result<> save_to_file(const World& world, const std::string& levelName, const std::string& filePath);

    /// Convenience: load directly from a file into a World.
    static Result<> load_from_file(const std::string& filePath, World& world);
};

NOC_RESTORE_DLL_WARNINGS
