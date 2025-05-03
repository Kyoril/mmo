#include "selected_map_entity.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "editors/world_editor/world_editor_instance.h"
#include "scene_graph/mesh_manager.h"

namespace mmo
{
    SelectedMapEntity::SelectedMapEntity(MapEntity& entity, const std::function<void(Selectable&)>& duplication)
        : m_entity(entity)
		, m_duplication(duplication)
    {
    }

    void SelectedMapEntity::Translate(const Vector3& delta)
    {
        m_entity.GetSceneNode().Translate(delta, TransformSpace::World);
        m_entity.MarkModified();
        positionChanged(*this);
    }

    void SelectedMapEntity::Rotate(const Quaternion& delta)
    {
        m_entity.GetSceneNode().Rotate(delta, TransformSpace::Parent);
        m_entity.MarkModified();
        rotationChanged(*this);
    }

    void SelectedMapEntity::Scale(const Vector3& delta)
    {
        m_entity.GetSceneNode().Scale(delta);
        m_entity.MarkModified();
        scaleChanged(*this);
    }

    void SelectedMapEntity::Remove()
    {
        m_entity.remove(m_entity);
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

    void SelectedMapEntity::SetPosition(const Vector3& position) const
    {
        m_entity.GetSceneNode().SetPosition(position);
    	m_entity.MarkModified();
		positionChanged(*this);
    }

    void SelectedMapEntity::SetOrientation(const Quaternion& orientation) const
    {
        m_entity.GetSceneNode().SetOrientation(orientation);
        m_entity.MarkModified();
        rotationChanged(*this);
    }

    void SelectedMapEntity::SetScale(const Vector3& scale) const
    {
		m_entity.GetSceneNode().SetScale(scale);
        m_entity.MarkModified();
		scaleChanged(*this);
    }

    void SelectedMapEntity::Duplicate()
    {
        if (m_duplication)
		{
			m_duplication(*this);
		}
    }

    void SelectedMapEntity::Visit(SelectableVisitor& visitor)
    {
        visitor.Visit(*this);
    }

    SelectedTerrainTile::SelectedTerrainTile(terrain::Tile& tile)
	    : Selectable()
		, m_tile(tile)
    {

    }

    void SelectedTerrainTile::Visit(SelectableVisitor& visitor)
    {
        visitor.Visit(*this);
    }

    void SelectedTerrainTile::Deselect()
    {
    }

    Vector3 SelectedTerrainTile::GetPosition() const
	{
		return Vector3::Zero;
    }

    Quaternion SelectedTerrainTile::GetOrientation() const
	{
		return Quaternion::Identity;
    }

    Vector3 SelectedTerrainTile::GetScale() const
    {
		return Vector3::UnitScale;
    }

    SelectedUnitSpawn::SelectedUnitSpawn(proto::UnitSpawnEntry& entry, const proto::UnitManager& units, const proto::ModelDataManager& models, SceneNode& node, Entity& entity, const std::function<void(Selectable&)>& duplication,
        const std::function<void(const proto::UnitSpawnEntry&)>& removal)
		: Selectable()
		, m_entry(entry)
		, m_units(units)
		, m_models(models)
        , m_node(node)
		, m_entity(entity)
		, m_duplication(duplication)
		, m_removal(removal)
    {
    }

    void SelectedUnitSpawn::Visit(SelectableVisitor& visitor)
    {
		visitor.Visit(*this);
    }

    void SelectedUnitSpawn::Duplicate()
    {
        if (m_duplication)
        {
            m_duplication(*this);
        }
    }

    void SelectedUnitSpawn::Translate(const Vector3& delta)
    {
        m_node.Translate(delta, TransformSpace::World);

		m_entry.set_positionx(m_entry.positionx() + delta.x);
        m_entry.set_positiony(m_entry.positiony() + delta.y);
        m_entry.set_positionz(m_entry.positionz() + delta.z);
        positionChanged(*this);
    }

    void SelectedUnitSpawn::Rotate(const Quaternion& delta)
    {
        m_node.Rotate(delta, TransformSpace::Parent);

	    auto rot = Quaternion(Radian(m_entry.rotation()), Vector3::UnitY);
        rot = rot * delta;
		m_entry.set_rotation(rot.GetYaw().GetValueRadians());
        rotationChanged(*this);
    }

    void SelectedUnitSpawn::Scale(const Vector3& delta)
    {
        
    }

    void SelectedUnitSpawn::Remove()
    {
        m_removal(m_entry);
    }

    void SelectedUnitSpawn::Deselect()
    {
    }

    void SelectedUnitSpawn::SetPosition(const Vector3& position) const
    {
        m_node.SetPosition(position);
        m_entry.set_positionx(position.x);
        m_entry.set_positiony(position.y);
        m_entry.set_positionz(position.z);
        positionChanged(*this);
    }

    void SelectedUnitSpawn::SetOrientation(const Quaternion& orientation) const
    {
        m_node.SetOrientation(orientation);
        m_entry.set_rotation(orientation.GetYaw().GetValueRadians());
        rotationChanged(*this);
    }

    void SelectedUnitSpawn::SetScale(const Vector3& scale) const
    {
    }

    Vector3 SelectedUnitSpawn::GetPosition() const
    {
        return Vector3(m_entry.positionx(), m_entry.positiony(), m_entry.positionz());
    }

    Quaternion SelectedUnitSpawn::GetOrientation() const
    {
        return Quaternion(Radian(m_entry.rotation()), Vector3::UnitY);
    }

    Vector3 SelectedUnitSpawn::GetScale() const
    {
        return Vector3::UnitScale;
    }

    void SelectedUnitSpawn::RefreshEntity()
    {
        // Get unit entry
        const proto::UnitEntry* unit = m_units.getById(m_entry.unitentry());
        if (!unit)
        {
            return;
        }

        const uint32 modelId = unit->malemodel() ? unit->malemodel() : unit->femalemodel();
        if (modelId == 0)
        {
            return;
        }

		const proto::ModelDataEntry* model = m_models.getById(modelId);
        if (!model)
        {
            return;
        }

        MeshPtr mesh = MeshManager::Get().Load(model->filename());
        if (!mesh)
        {
            return;
        }

        m_entity.SetMesh(mesh);
    }


    SelectedObjectSpawn::SelectedObjectSpawn(proto::ObjectSpawnEntry& entry, const proto::ObjectManager& objects, const proto::ObjectDisplayManager& models, SceneNode& node, Entity& entity, const std::function<void(Selectable&)>& duplication,
        const std::function<void(const proto::ObjectSpawnEntry&)>& removal)
        : Selectable()
        , m_entry(entry)
        , m_units(objects)
        , m_models(models)
        , m_node(node)
        , m_entity(entity)
        , m_duplication(duplication)
        , m_removal(removal)
    {
    }

    void SelectedObjectSpawn::Visit(SelectableVisitor& visitor)
    {
        visitor.Visit(*this);
    }

    void SelectedObjectSpawn::Duplicate()
    {
        if (m_duplication)
        {
            m_duplication(*this);
        }
    }

    void SelectedObjectSpawn::Translate(const Vector3& delta)
    {
        m_node.Translate(delta, TransformSpace::World);

        m_entry.mutable_location()->set_positionx(m_entry.location().positionx() + delta.x);
        m_entry.mutable_location()->set_positiony(m_entry.location().positiony() + delta.y);
        m_entry.mutable_location()->set_positionz(m_entry.location().positionz() + delta.z);
        positionChanged(*this);
    }

    void SelectedObjectSpawn::Rotate(const Quaternion& delta)
    {
        m_node.Rotate(delta, TransformSpace::Parent);

        const auto orientation = m_node.GetDerivedOrientation();
        m_entry.mutable_location()->set_rotationx(orientation.x);
        m_entry.mutable_location()->set_rotationy(orientation.y);
        m_entry.mutable_location()->set_rotationz(orientation.z);
        m_entry.mutable_location()->set_rotationw(orientation.w);
        rotationChanged(*this);
    }

    void SelectedObjectSpawn::Scale(const Vector3& delta)
    {

    }

    void SelectedObjectSpawn::Remove()
    {
        m_removal(m_entry);
    }

    void SelectedObjectSpawn::Deselect()
    {
    }

    void SelectedObjectSpawn::SetPosition(const Vector3& position) const
    {
        m_node.SetPosition(position);
        m_entry.mutable_location()->set_positionx(position.x);
        m_entry.mutable_location()->set_positiony(position.y);
        m_entry.mutable_location()->set_positionz(position.z);
        positionChanged(*this);
    }

    void SelectedObjectSpawn::SetOrientation(const Quaternion& orientation) const
    {
        m_node.SetOrientation(orientation);
        m_entry.mutable_location()->set_rotationx(orientation.x);
        m_entry.mutable_location()->set_rotationy(orientation.y);
        m_entry.mutable_location()->set_rotationz(orientation.z);
        m_entry.mutable_location()->set_rotationw(orientation.w);
        rotationChanged(*this);
    }

    void SelectedObjectSpawn::SetScale(const Vector3& scale) const
    {
    }

    Vector3 SelectedObjectSpawn::GetPosition() const
    {
        return Vector3(m_entry.location().positionx(), m_entry.location().positiony(), m_entry.location().positionz());
    }

    Quaternion SelectedObjectSpawn::GetOrientation() const
    {
        return Quaternion(m_entry.location().rotationw(), m_entry.location().rotationx(), m_entry.location().rotationy(), m_entry.location().rotationz());
    }

    Vector3 SelectedObjectSpawn::GetScale() const
    {
        return Vector3::UnitScale;
    }

    void SelectedObjectSpawn::RefreshEntity()
    {
        // Get unit entry
        const proto::ObjectEntry* object = m_units.getById(m_entry.objectentry());
        if (!object)
        {
            return;
        }

        const uint32 modelId = object->displayid();
        if (modelId == 0)
        {
            return;
        }

        const proto::ObjectDisplayEntry* model = m_models.getById(modelId);
        if (!model)
        {
            return;
        }

        MeshPtr mesh = MeshManager::Get().Load(model->filename());
        if (!mesh)
        {
            return;
        }

        m_entity.SetMesh(mesh);
    }
}
