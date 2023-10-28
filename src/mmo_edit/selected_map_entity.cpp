#include "selected_map_entity.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "editors/world_editor/world_editor_instance.h"

namespace mmo
{
    SelectedMapEntity::SelectedMapEntity(MapEntity& entity)
        : m_entity(entity)
    {
    }

    void SelectedMapEntity::Translate(const Vector3& delta)
    {
        m_entity.GetSceneNode().Translate(delta, TransformSpace::Local);
        positionChanged(*this);
    }

    void SelectedMapEntity::Rotate(const Quaternion& delta)
    {
        m_entity.GetSceneNode().Rotate(delta, TransformSpace::Local);
        rotationChanged(*this);
    }

    void SelectedMapEntity::Scale(const Vector3& delta)
    {
        m_entity.GetSceneNode().Scale(delta);
        scaleChanged(*this);
    }

    void SelectedMapEntity::Remove()
    {
        // TODO
    }

    void SelectedMapEntity::Deselect()
    {
        // TODO
    }

    Vector3 SelectedMapEntity::GetPosition() const
    {
        return m_entity.GetSceneNode().GetDerivedPosition();
    }

    Quaternion SelectedMapEntity::GetOrientation() const
    {
        return m_entity.GetSceneNode().GetDerivedOrientation();
    }

    Vector3 SelectedMapEntity::GetScale() const
    {
        return m_entity.GetSceneNode().GetDerivedScale();
    }
}
