#include "selected_entity.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
    SelectedEntity::SelectedEntity(Entity& entity)
        : m_entity(entity)
    {
    }

    void SelectedEntity::Translate(const Vector3& delta)
    {
        m_entity.GetParentSceneNode()->Translate(delta, TransformSpace::Local);

        // Raise event
        positionChanged(*this);
    }

    void SelectedEntity::Rotate(const Quaternion& delta)
    {
        m_entity.GetParentSceneNode()->Rotate(delta, TransformSpace::Local);

        rotationChanged(*this);
    }

    void SelectedEntity::Scale(const Vector3& delta)
    {
        m_entity.GetParentSceneNode()->Scale(delta);

        scaleChanged(*this);
    }

    void SelectedEntity::Remove()
    {
        // TODO
    }

    void SelectedEntity::Deselect()
    {
        // TODO
    }

    Vector3 SelectedEntity::GetPosition() const
    {
        return m_entity.GetParentSceneNode()->GetDerivedPosition();
    }

    Quaternion SelectedEntity::GetOrientation() const
    {
        return m_entity.GetParentSceneNode()->GetDerivedOrientation();
    }

    Vector3 SelectedEntity::GetScale() const
    {
        return m_entity.GetParentSceneNode()->GetDerivedScale();
    }
}
