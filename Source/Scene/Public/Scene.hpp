#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Rendering/Public/Renderable.hpp"
#include "SceneNode.hpp"

#include <memory>
#include <vector>

NOC_SUPPRESS_DLL_WARNINGS

/// Owns a scene graph (tree of SceneNode) and provides iteration
/// over renderable objects for the renderer.
class NOC_EXPORT Scene
{
  public:
    Scene();

    /// Returns the root node. All scene objects are children of the root.
    SceneNode* get_root() noexcept
    {
        return m_root.get();
    }
    const SceneNode* get_root() const noexcept
    {
        return m_root.get();
    }

    /// Walks the scene graph and collects all nodes that have a mesh,
    /// producing a flat list of Renderables with pre-computed world matrices.
    std::vector<Renderable> collect_renderables() const;

  private:
    void collect_renderables_recursive(const SceneNode* node, const DirectX::XMMATRIX& parentWorld,
                                       std::vector<Renderable>& out) const;

    std::unique_ptr<SceneNode> m_root;
};

NOC_RESTORE_DLL_WARNINGS
