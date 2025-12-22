#pragma once

#include "Expected.hpp"

#include <fstream>
#include <string_view>
#include <filesystem>
#include <vector>

static inline Result<std::vector<char>> read_file(std::string_view filename)
{
    std::filesystem::path filePath{filename.data()};
    std::ifstream file{filePath, std::ios::ate | std::ios::binary};

    if (!file.is_open())
    {
        return make_error("Failed to open file: " + filePath.string(), ErrorCode::FileReadFailed);
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
}
