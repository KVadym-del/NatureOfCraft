#include "../Public/SceneNode.hpp"

using namespace DirectX;

SceneNode::SceneNode(std::string name) : m_name(std::move(name))
{}

SceneNode* SceneNode::add_child(std::string name)
{
    auto child = std::make_unique<SceneNode>(std::move(name));
    child->m_parent = this;
    m_children.push_back(std::move(child));
    return m_children.back().get();
}

bool SceneNode::remove_child(SceneNode* child)
{
    for (auto it = m_children.begin(); it != m_children.end(); ++it)
    {
        if (it->get() == child)
        {
            (*it)->m_parent = nullptr;
            m_children.erase(it);
            return true;
        }
    }
    return false;
}

XMMATRIX SceneNode::get_world_matrix() const noexcept
{
    XMMATRIX local = m_transform.get_local_matrix();
    if (m_parent)
    {
        return XMMatrixMultiply(local, m_parent->get_world_matrix());
    }
    return local;
}
