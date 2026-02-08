
#pragma once
#include <array>
#include <functional>
#include <vector>

#include <vulkan/vulkan.h>

#include <DirectXMath.h>

using namespace DirectX;

struct Vertex
{
    XMFLOAT3 pos;
    XMFLOAT3 normal;
    XMFLOAT2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }

    bool operator==(const Vertex& other) const
    {
        return pos.x == other.pos.x && pos.y == other.pos.y && pos.z == other.pos.z && normal.x == other.normal.x &&
               normal.y == other.normal.y && normal.z == other.normal.z && texCoord.x == other.texCoord.x &&
               texCoord.y == other.texCoord.y;
    }
};

struct Mesh
{
    VkBuffer vertexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory vertexBufferMemory{VK_NULL_HANDLE};
    VkBuffer indexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory indexBufferMemory{VK_NULL_HANDLE};
    uint32_t indexCount{};
};