#pragma once
#include "../../Core/Public/Core.hpp"
#include "Transform.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

NOC_SUPPRESS_DLL_WARNINGS

/// A node in a scene graph hierarchy.
/// Each node has a local Transform, an optional mesh reference (by index),
/// and zero or more child nodes. The world matrix is computed by walking
/// the parent chain.
class NOC_EXPORT SceneNode
{
  public:
    explicit SceneNode(std::string name = "");

    // Non-copyable (owns unique_ptr children), movable
    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;
    SceneNode(SceneNode&&) noexcept = default;
    SceneNode& operator=(SceneNode&&) noexcept = default;
    ~SceneNode() = default;

    /// Creates a new child node and returns a non-owning pointer to it.
    SceneNode* add_child(std::string name = "");

    /// Removes a child node by pointer. Returns true if found and removed.
    bool remove_child(SceneNode* child);

    /// Computes the world matrix by multiplying all ancestor local matrices.
    DirectX::XMMATRIX get_world_matrix() const noexcept;

    // --- Transform ---

    Transform& get_transform() noexcept
    {
        return m_transform;
    }
    const Transform& get_transform() const noexcept
    {
        return m_transform;
    }

    // --- Mesh ---

    void set_mesh_index(int32_t index) noexcept
    {
        m_meshIndex = index;
    }
    int32_t get_mesh_index() const noexcept
    {
        return m_meshIndex;
    }
    bool has_mesh() const noexcept
    {
        return m_meshIndex >= 0;
    }

    // --- Hierarchy ---

    const std::string& get_name() const noexcept
    {
        return m_name;
    }
    SceneNode* get_parent() const noexcept
    {
        return m_parent;
    }
    const std::vector<std::unique_ptr<SceneNode>>& get_children() const noexcept
    {
        return m_children;
    }

  private:
    std::string m_name;
    Transform m_transform{};
    int32_t m_meshIndex{-1};

    SceneNode* m_parent{nullptr};
    std::vector<std::unique_ptr<SceneNode>> m_children;
};

NOC_RESTORE_DLL_WARNINGS
