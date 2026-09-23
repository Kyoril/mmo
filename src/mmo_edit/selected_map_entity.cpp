#include "selected_map_entity.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "editors/world_editor/world_editor_instance.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/material_manager.h"
#include "math/math_utils.h"
#include "math/degree.h"

#include <algorithm>
#include <cmath>

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

	SelectedAreaTrigger::SelectedAreaTrigger(proto::AreaTriggerEntry& entry, SceneNode& node, ManualRenderObject& renderObject,
		const std::function<void(Selectable&)>& duplication, const std::function<void(const proto::AreaTriggerEntry&)>& removal)
		: m_entry(entry)
		, m_node(node)
		, m_renderObject(renderObject)
		, m_duplication(duplication)
		, m_removal(removal)
	{
	}

	void SelectedAreaTrigger::Visit(SelectableVisitor& visitor)
	{
		visitor.Visit(*this);
	}

	void SelectedAreaTrigger::Duplicate()
	{
		// Not supported for area triggers
	}

	void SelectedAreaTrigger::Translate(const Vector3& delta)
	{
		m_entry.set_x(m_entry.x() + delta.x);
		m_entry.set_y(m_entry.y() + delta.y);
		m_entry.set_z(m_entry.z() + delta.z);

		m_node.SetPosition(Vector3(m_entry.x(), m_entry.y(), m_entry.z()));
		positionChanged(*this);
	}

	void SelectedAreaTrigger::Rotate(const Quaternion& delta)
	{
		if (!m_entry.has_box_x())
		{
			return; // Sphere triggers don't rotate
		}

		// Update box orientation
		Quaternion current = GetOrientation();
		Quaternion newRotation = delta * current;
		newRotation.Normalize();

		// Convert to Euler angle for box_o field (rotation around Y axis)
		Radian yaw = newRotation.GetYaw();
		m_entry.set_box_o(yaw.GetValueRadians());

		m_node.SetOrientation(newRotation);
		RefreshVisual();
		rotationChanged(*this);
	}

	void SelectedAreaTrigger::Scale(const Vector3& delta)
	{
		if (m_entry.has_radius())
		{
			// Sphere: use average scale
			const float avgScale = (delta.x + delta.y + delta.z) / 3.0f;
			m_entry.set_radius(m_entry.radius() * avgScale);
		}
		else
		{
			// Box: scale each dimension
			m_entry.set_box_x(m_entry.box_x() * delta.x);
			m_entry.set_box_y(m_entry.box_y() * delta.y);
			m_entry.set_box_z(m_entry.box_z() * delta.z);
		}

		RefreshVisual();
		scaleChanged(*this);
	}

	void SelectedAreaTrigger::Remove()
	{
		m_removal(m_entry);
	}

	void SelectedAreaTrigger::Deselect()
	{
		// Nothing special needed
	}

	void SelectedAreaTrigger::SetPosition(const Vector3& position) const
	{
		m_entry.set_x(position.x);
		m_entry.set_y(position.y);
		m_entry.set_z(position.z);
		m_node.SetPosition(position);
	}

	void SelectedAreaTrigger::SetOrientation(const Quaternion& orientation) const
	{
		if (!m_entry.has_box_x())
		{
			return; // Sphere triggers don't rotate
		}

		Radian yaw = orientation.GetYaw();
		m_entry.set_box_o(yaw.GetValueRadians());
		m_node.SetOrientation(orientation);
	}

	void SelectedAreaTrigger::SetScale(const Vector3& scale) const
	{
		if (m_entry.has_radius())
		{
			const float avgScale = (scale.x + scale.y + scale.z) / 3.0f;
			m_entry.set_radius(avgScale);
		}
		else
		{
			m_entry.set_box_x(scale.x);
			m_entry.set_box_y(scale.y);
			m_entry.set_box_z(scale.z);
		}
	}

	Vector3 SelectedAreaTrigger::GetPosition() const
	{
		return Vector3(m_entry.x(), m_entry.y(), m_entry.z());
	}

	Quaternion SelectedAreaTrigger::GetOrientation() const
	{
		if (m_entry.has_box_x() && m_entry.has_box_o())
		{
			return Quaternion(Radian(m_entry.box_o()), Vector3::UnitY);
		}
		return Quaternion::Identity;
	}

	Vector3 SelectedAreaTrigger::GetScale() const
	{
		if (m_entry.has_radius())
		{
			const float r = m_entry.radius();
			return Vector3(r, r, r);
		}
		return Vector3(m_entry.box_x(), m_entry.box_y(), m_entry.box_z());
	}

	void SelectedAreaTrigger::RefreshVisual()
	{
		// Clear existing geometry
		m_renderObject.Clear();
		
		// Rebuild wireframe with updated dimensions
		auto lineListOp = m_renderObject.AddLineListOperation(MaterialManager::Get().Load("Editor/Wireframe"));
		
		const bool isSphere = m_entry.has_radius();
		if (isSphere)
		{
			// Draw sphere wireframe
			const float radius = m_entry.radius();
			const int segments = 16;
			const int rings = 8;

			// Vertical circles
			for (int i = 0; i < segments; ++i)
			{
				const float angle1 = (float)i / segments * 2.0f * Pi;
				const float angle2 = (float)(i + 1) / segments * 2.0f * Pi;

				for (int j = 0; j < rings; ++j)
				{
					const float ring1 = (float)j / rings * Pi - Pi / 2.0f;
					const float ring2 = (float)(j + 1) / rings * Pi - Pi / 2.0f;

					Vector3 p1(radius * cos(ring1) * cos(angle1), radius * sin(ring1), radius * cos(ring1) * sin(angle1));
					Vector3 p2(radius * cos(ring2) * cos(angle1), radius * sin(ring2), radius * cos(ring2) * sin(angle1));
					Vector3 p3(radius * cos(ring1) * cos(angle2), radius * sin(ring1), radius * cos(ring1) * sin(angle2));

					lineListOp->AddLine(p1, p2);
					lineListOp->AddLine(p1, p3);
				}
			}
		}
		else
		{
			// Draw box wireframe
			const float hx = m_entry.box_x() / 2.0f;
			const float hy = m_entry.box_y() / 2.0f;
			const float hz = m_entry.box_z() / 2.0f;

			Vector3 corners[8] = {
				Vector3(-hx, -hy, -hz), Vector3(hx, -hy, -hz),
				Vector3(hx, -hy, hz), Vector3(-hx, -hy, hz),
				Vector3(-hx, hy, -hz), Vector3(hx, hy, -hz),
				Vector3(hx, hy, hz), Vector3(-hx, hy, hz)
			};

			// Bottom face
			for (int i = 0; i < 4; ++i)
			{
				lineListOp->AddLine(corners[i], corners[(i + 1) % 4]);
			}

			// Top face
			for (int i = 0; i < 4; ++i)
			{
				lineListOp->AddLine(corners[i + 4], corners[(i + 1) % 4 + 4]);
			}

			// Vertical edges
			for (int i = 0; i < 4; ++i)
			{
				lineListOp->AddLine(corners[i], corners[i + 4]);
			}
		}
	}

	void BuildFogVolumeWireframe(ManualRenderObject& renderObject, const FogVolume& volume)
	{
		renderObject.Clear();

		const auto toChannel = [](const float value) -> uint32
		{
			return static_cast<uint32>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
		};

		const uint32 color = 0xFF000000u | (toChannel(volume.color.x) << 16) | (toChannel(volume.color.y) << 8) | toChannel(volume.color.z);

		auto lineListOp = renderObject.AddLineListOperation(MaterialManager::Get().Load("Editor/Wireframe.hmat"));

		// FogVolume::size holds the full extents; the renderer works with half of it.
		const Vector3 halfSize = volume.size * 0.5f;

		if (volume.shape == FogVolumeShape::Ellipsoid)
		{
			constexpr int32 segments = 48;
			for (int32 i = 0; i < segments; ++i)
			{
				const float a1 = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * Pi;
				const float a2 = static_cast<float>(i + 1) / static_cast<float>(segments) * 2.0f * Pi;
				const float c1 = std::cos(a1);
				const float s1 = std::sin(a1);
				const float c2 = std::cos(a2);
				const float s2 = std::sin(a2);

				// XY plane
				lineListOp->AddLine(Vector3(c1 * halfSize.x, s1 * halfSize.y, 0.0f), Vector3(c2 * halfSize.x, s2 * halfSize.y, 0.0f)).SetColor(color);

				// YZ plane
				lineListOp->AddLine(Vector3(0.0f, c1 * halfSize.y, s1 * halfSize.z), Vector3(0.0f, c2 * halfSize.y, s2 * halfSize.z)).SetColor(color);

				// XZ plane
				lineListOp->AddLine(Vector3(c1 * halfSize.x, 0.0f, s1 * halfSize.z), Vector3(c2 * halfSize.x, 0.0f, s2 * halfSize.z)).SetColor(color);
			}
		}
		else
		{
			const float hx = halfSize.x;
			const float hy = halfSize.y;
			const float hz = halfSize.z;

			const Vector3 corners[8] = {
				Vector3(-hx, -hy, -hz), Vector3(hx, -hy, -hz),
				Vector3(hx, -hy, hz), Vector3(-hx, -hy, hz),
				Vector3(-hx, hy, -hz), Vector3(hx, hy, -hz),
				Vector3(hx, hy, hz), Vector3(-hx, hy, hz)
			};

			for (int32 i = 0; i < 4; ++i)
			{
				// Bottom face, top face and the vertical edge joining them.
				lineListOp->AddLine(corners[i], corners[(i + 1) % 4]).SetColor(color);
				lineListOp->AddLine(corners[i + 4], corners[(i + 1) % 4 + 4]).SetColor(color);
				lineListOp->AddLine(corners[i], corners[i + 4]).SetColor(color);
			}
		}
	}

	float WrapFogVolumeYaw(const float degrees)
	{
		if (!std::isfinite(degrees))
		{
			return 0.0f;
		}

		float wrapped = std::fmod(degrees, 360.0f);
		if (wrapped < 0.0f)
		{
			wrapped += 360.0f;
		}

		return wrapped >= 360.0f ? 0.0f : wrapped;
	}

	float ExtractFogVolumeYawDegrees(const Quaternion& orientation)
	{
		// Quaternion(Degree(yaw), UnitY) * UnitX == (cos(yaw), 0, -sin(yaw)).
		const Vector3 rotatedX = orientation * Vector3::UnitX;
		if (std::abs(rotatedX.x) < 1.0e-6f && std::abs(rotatedX.z) < 1.0e-6f)
		{
			return 0.0f;
		}

		const float radians = std::atan2(-rotatedX.z, rotatedX.x);
		return WrapFogVolumeYaw(radians * 180.0f / Pi);
	}

	SelectedFogVolume::SelectedFogVolume(const uint32 volumeId, std::vector<FogVolume>& volumes, IWorldEditor& worldEditor, SceneNode& node, ManualRenderObject& renderObject,
		const std::function<void(Selectable&)>& duplication, const std::function<void(uint32)>& removal)
		: m_volumeId(volumeId)
		, m_volumes(volumes)
		, m_worldEditor(worldEditor)
		, m_node(node)
		, m_renderObject(renderObject)
		, m_duplication(duplication)
		, m_removal(removal)
	{
	}

	void SelectedFogVolume::Visit(SelectableVisitor& visitor)
	{
		visitor.Visit(*this);
	}

	void SelectedFogVolume::Duplicate()
	{
		if (m_duplication)
		{
			m_duplication(*this);
		}
	}

	void SelectedFogVolume::Translate(const Vector3& delta)
	{
		FogVolume* volume = FindVolume();
		if (!volume)
		{
			return;
		}

		volume->position += delta;
		SanitizeFogVolume(*volume);
		SyncNode(*volume);
		m_worldEditor.MarkFogVolumesChanged();
		positionChanged(*this);
	}

	void SelectedFogVolume::Rotate(const Quaternion& delta)
	{
		FogVolume* volume = FindVolume();
		if (!volume)
		{
			return;
		}

		// Only the rotation about +Y is meaningful for a fog volume; pitch and roll are dropped.
		float deltaYaw = ExtractFogVolumeYawDegrees(delta);
		if (deltaYaw > 180.0f)
		{
			deltaYaw -= 360.0f;
		}

		volume->yaw = WrapFogVolumeYaw(volume->yaw + deltaYaw);
		SyncNode(*volume);
		m_worldEditor.MarkFogVolumesChanged();
		rotationChanged(*this);
	}

	void SelectedFogVolume::Scale(const Vector3& delta)
	{
		FogVolume* volume = FindVolume();
		if (!volume)
		{
			return;
		}

		volume->size = Vector3(
			std::max(volume->size.x * delta.x, 0.5f),
			std::max(volume->size.y * delta.y, 0.5f),
			std::max(volume->size.z * delta.z, 0.5f));
		SanitizeFogVolume(*volume);
		BuildFogVolumeWireframe(m_renderObject, *volume);
		m_worldEditor.MarkFogVolumesChanged();
		scaleChanged(*this);
	}

	void SelectedFogVolume::Remove()
	{
		if (m_removal)
		{
			m_removal(m_volumeId);
		}
	}

	void SelectedFogVolume::Deselect()
	{
		// The node and render object may already be destroyed at this point (Remove() runs first),
		// so there is nothing to touch here.
	}

	void SelectedFogVolume::SetPosition(const Vector3& position) const
	{
		FogVolume* volume = FindVolume();
		if (!volume)
		{
			return;
		}

		volume->position = position;
		SanitizeFogVolume(*volume);
		SyncNode(*volume);
		m_worldEditor.MarkFogVolumesChanged();
	}

	void SelectedFogVolume::SetOrientation(const Quaternion& orientation) const
	{
		FogVolume* volume = FindVolume();
		if (!volume)
		{
			return;
		}

		volume->yaw = ExtractFogVolumeYawDegrees(orientation);
		SyncNode(*volume);
		m_worldEditor.MarkFogVolumesChanged();
	}

	void SelectedFogVolume::SetScale(const Vector3& scale) const
	{
		FogVolume* volume = FindVolume();
		if (!volume)
		{
			return;
		}

		volume->size = Vector3(std::max(scale.x, 0.5f), std::max(scale.y, 0.5f), std::max(scale.z, 0.5f));
		SanitizeFogVolume(*volume);
		BuildFogVolumeWireframe(m_renderObject, *volume);
		m_worldEditor.MarkFogVolumesChanged();
	}

	Vector3 SelectedFogVolume::GetPosition() const
	{
		const FogVolume* volume = FindVolume();
		return volume ? volume->position : Vector3::Zero;
	}

	Quaternion SelectedFogVolume::GetOrientation() const
	{
		const FogVolume* volume = FindVolume();
		return volume ? Quaternion(Degree(volume->yaw), Vector3::UnitY) : Quaternion::Identity;
	}

	Vector3 SelectedFogVolume::GetScale() const
	{
		const FogVolume* volume = FindVolume();
		return volume ? volume->size : Vector3(1.0f, 1.0f, 1.0f);
	}

	void SelectedFogVolume::NotifyTransformChanged()
	{
		positionChanged(*this);
		rotationChanged(*this);
		scaleChanged(*this);
	}

	FogVolume* SelectedFogVolume::FindVolume() const
	{
		const auto it = std::find_if(m_volumes.begin(), m_volumes.end(), [this](const FogVolume& volume)
		{
			return volume.id == m_volumeId;
		});

		return it != m_volumes.end() ? &*it : nullptr;
	}

	void SelectedFogVolume::SyncNode(const FogVolume& volume) const
	{
		m_node.SetPosition(volume.position);
		m_node.SetOrientation(Quaternion(Degree(volume.yaw), Vector3::UnitY));
	}
}
