#include "../Public/Scene.hpp"

using namespace DirectX;

Scene::Scene() : m_root(std::make_unique<SceneNode>("Root"))
{}

std::vector<Renderable> Scene::collect_renderables() const
{
    std::vector<Renderable> renderables;
    collect_renderables_recursive(m_root.get(), XMMatrixIdentity(), renderables);
    return renderables;
}

void Scene::collect_renderables_recursive(const SceneNode* node, const XMMATRIX& parentWorld,
                                          std::vector<Renderable>& out) const
{
    XMMATRIX localMatrix = node->get_transform().get_local_matrix();
    XMMATRIX worldMatrix = XMMatrixMultiply(localMatrix, parentWorld);

    if (node->has_mesh())
    {
        Renderable r{};
        XMStoreFloat4x4(&r.worldMatrix, worldMatrix);
        r.meshIndex = static_cast<uint32_t>(node->get_mesh_index());
        out.push_back(r);
    }

    for (const auto& child : node->get_children())
    {
        collect_renderables_recursive(child.get(), worldMatrix, out);
    }
}
