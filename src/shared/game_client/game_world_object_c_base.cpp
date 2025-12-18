
#include "game_world_object_c_base.h"

#include "game/movement_info.h"
#include "scene_graph/scene.h"

#include "net_client.h"
#include "client_data/project.h"
#include "game/object_info.h"
#include "scene_graph/mesh_manager.h"
#include "game_player_c.h"
#include "game/quest.h"

namespace mmo
{
	GameWorldObjectC::GameWorldObjectC(Scene& scene, const proto_client::Project& project, NetClient& netDriver, uint32 map)
		: GameObjectC(scene, project, map)
		, m_netDriver(netDriver)
	{
	}

	void GameWorldObjectC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::WorldObjectFieldCount);
	}

	void GameWorldObjectC::Deserialize(io::Reader& reader, bool complete)
	{
		uint32 updateFlags = 0;
		if (!(reader >> io::read<uint32>(updateFlags)))
		{
			return;
		}

		MovementInfo info;

		ASSERT(!complete || (updateFlags & object_update_flags::HasMovementInfo) != 0);
		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			if (!(reader >> info))
			{
				return;
			}
		}

		if (complete)
		{
			if (!(m_fieldMap.DeserializeComplete(reader)))
			{
				ASSERT(false);
			}

			switch (GetType())
			{
			case GameWorldObjectType::Chest:
				m_typeData = std::make_unique<GameWorldObjectC_Type_Chest>();
				break;
			case GameWorldObjectType::Door:
				m_typeData = std::make_unique<GameWorldObjectC_Type_Door>();
				break;
			default:
				ASSERT(false);
				break;
			}

			ASSERT(GetGuid() > 0);
			SetupSceneObjects();
		}
		else
		{
			if (!(m_fieldMap.DeserializeChanges(reader)))
			{
				ASSERT(false);
			}

			// This field has to be constant!
			ASSERT(!m_fieldMap.IsFieldMarkedAsChanged(object_fields::ObjectTypeId));

			if (!complete && m_fieldMap.IsFieldMarkedAsChanged(object_fields::ObjectDisplayId))
			{
				OnDisplayIdChanged();
			}

			if (!complete && m_fieldMap.IsFieldMarkedAsChanged(object_fields::Scale))
			{
				//OnScaleChanged();
			}

			HandleFieldMapChanges();
		}

		if (complete || m_fieldMap.IsFieldMarkedAsChanged(object_fields::Entry))
		{
			OnEntryChanged();
		}

		m_fieldMap.MarkAllAsUnchanged();

		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			m_sceneNode->SetDerivedPosition(info.position);
			m_sceneNode->SetDerivedOrientation(Quaternion(
				Get<float>(object_fields::RotationW),
				Get<float>(object_fields::RotationX),
				Get<float>(object_fields::RotationY),
				Get<float>(object_fields::RotationZ)));
		}
	}

	void GameWorldObjectC::NotifyObjectData(const ObjectInfo& data)
	{
		m_entry = &data;

		OnDisplayIdChanged();
	}

	void GameWorldObjectC::SetupSceneObjects()
	{
		GameObjectC::SetupSceneObjects();

		m_entity = m_scene.CreateEntity(std::to_string(GetGuid()), "Models/Cube/Cube.hmsh");
		m_entity->SetUserObject(this);
		m_entity->SetQueryFlags(0x00000002);
		m_entityOffsetNode->AttachObject(*m_entity);
	}

	void GameWorldObjectC::OnDisplayIdChanged()
	{
		const uint32 displayId = Get<uint32>(object_fields::ObjectDisplayId);
		if (displayId == 0 && !m_entry)
		{
			return;
		}

		const proto_client::ObjectDisplayEntry* displayEntry = m_project.objectDisplays.getById(displayId);
		if (m_entity) m_entity->SetVisible(displayEntry != nullptr);
		if (!displayEntry)
		{
			return;
		}

		String meshFile = displayEntry->filename();

		// Update or create entity
		if (!m_entity)
		{
			m_entity = m_scene.CreateEntity(std::to_string(GetGuid()), meshFile);
			m_entity->SetUserObject(this);
			m_entity->SetQueryFlags(0x00000002);
			m_entityOffsetNode->AttachObject(*m_entity);
		}
		else
		{
			// Just update the mesh
			m_entity->SetMesh(MeshManager::Get().Load(meshFile));
		}
	}

	void GameWorldObjectC::OnEntryChanged()
	{
		const uint32 entryId = Get<uint32>(object_fields::Entry);
		if (entryId == 0 && !m_entry)
		{
			return;
		}

		if (m_entry && m_entry->id == entryId)
		{
			return;
		}

		m_netDriver.GetObjectData(entryId, static_pointer_cast<GameWorldObjectC>(shared_from_this()));
	}
}
