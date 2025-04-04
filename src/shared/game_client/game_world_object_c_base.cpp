
#include "game_world_object_c_base.h"

#include "game/movement_info.h"
#include "scene_graph/scene.h"

#include "net_client.h"
#include "game/object_info.h"

namespace mmo
{
	GameWorldObjectC_Base::GameWorldObjectC_Base(Scene& scene, const proto_client::Project& project, NetClient& netDriver)
		: GameObjectC(scene, project)
		, m_netDriver(netDriver)
	{
	}

	void GameWorldObjectC_Base::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::WorldObjectFieldCount);
	}

	void GameWorldObjectC_Base::Deserialize(io::Reader& reader, bool complete)
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
		}
		else
		{
			if (!(m_fieldMap.DeserializeChanges(reader)))
			{
				ASSERT(false);
			}

			if (!complete && m_fieldMap.IsFieldMarkedAsChanged(object_fields::DisplayId))
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

		ASSERT(GetGuid() > 0);
		if (complete)
		{
			SetupSceneObjects();
		}

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

	void GameWorldObjectC_Base::NotifyObjectData(const ObjectInfo& data)
	{
		m_entry = &data;

		// TODO: Do something special? :)
	}

	void GameWorldObjectC_Base::SetupSceneObjects()
	{
		GameObjectC::SetupSceneObjects();

		m_entity = m_scene.CreateEntity(std::to_string(GetGuid()), "Models/Cube/Cube.hmsh");
		m_entity->SetUserObject(this);
		m_entity->SetQueryFlags(0x00000002);
		m_entityOffsetNode->AttachObject(*m_entity);
	}

	void GameWorldObjectC_Base::OnDisplayIdChanged()
	{
		const uint32 displayId = Get<uint32>(object_fields::DisplayId);
		if (displayId == 0 && !m_entry)
		{
			return;
		}

		if (m_entry && m_entry->displayId == displayId)
		{
			return;
		}
	}

	void GameWorldObjectC_Base::OnEntryChanged()
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

		m_netDriver.GetObjectData(entryId, static_pointer_cast<GameWorldObjectC_Base>(shared_from_this()));
	}

	GameWorldObjectC_Chest::GameWorldObjectC_Chest(Scene& scene, const proto_client::Project& project, NetClient& netDriver)
		: GameWorldObjectC_Base(scene, project, netDriver)
	{
	}
}
