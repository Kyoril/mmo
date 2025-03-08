
#include "game_player_c.h"

#include "object_mgr.h"
#include "log/default_log_levels.h"
#include "scene_graph/scene.h"
#include "scene_graph/tag_point.h"

namespace mmo
{
	GamePlayerC::~GamePlayerC()
	{
		/*
		if (m_shieldEntity)
		{
			m_entity->DetachObjectFromBone(*m_shieldEntity);
			m_shieldAttachment = nullptr;

			m_scene.DestroyEntity(*m_shieldEntity);
			m_shieldEntity = nullptr;
		}


		if (m_weaponEntity)
		{
			m_entity->DetachObjectFromBone(*m_weaponEntity);
			m_weaponAttachment = nullptr;

			m_scene.DestroyEntity(*m_weaponEntity);
			m_weaponEntity = nullptr;
		}
		*/
	}

	void GamePlayerC::Deserialize(io::Reader& reader, bool complete)
	{
		GameUnitC::Deserialize(reader, complete);

		if (complete)
		{
			if (!(reader >> m_configuration))
			{
				ELOG("Failed to read player configuration");
			}

			OnDisplayIdChanged();
		}

		m_netDriver.GetPlayerName(GetGuid(), std::static_pointer_cast<GamePlayerC>(shared_from_this()));
	}

	void GamePlayerC::Update(float deltaTime)
	{
		GameUnitC::Update(deltaTime);
	}

	void GamePlayerC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::PlayerFieldCount);
	}

	const String& GamePlayerC::GetName() const
	{
		if (m_name.empty())
		{
			return GameObjectC::GetName();
		}

		return m_name;
	}

	uint8 GamePlayerC::GetAttributeCost(uint32 attribute) const
	{
		ASSERT(attribute < 5);

		const uint64 attributeCostPacked = Get<uint64>(object_fields::AttributePointCost);
		return (attributeCostPacked >> (attribute * 8)) & 0xFF;
	}

	void GamePlayerC::SetupSceneObjects()
	{
		GameUnitC::SetupSceneObjects();

		// TODO: For now we attach a shield to all player characters. This should be done based on the player's equipment though.
		ASSERT(m_entity);

		/*
		if (m_shieldEntity)
		{
			m_entity->DetachObjectFromBone(*m_shieldEntity);
			m_shieldAttachment = nullptr;

			m_scene.DestroyEntity(*m_shieldEntity);
			m_shieldEntity = nullptr;
		}

		if (m_entity->GetSkeleton()->HasBone("Back_M"))
		{
			m_shieldEntity = m_scene.CreateEntity(m_entity->GetName() + "_SHIELD", "Models/Character/Items/Weapons/Shield_Newbie_02.hmsh");
			m_shieldAttachment = m_entity->AttachObjectToBone("Back_M", *m_shieldEntity);
			m_shieldAttachment->SetScale(Vector3::UnitScale * 0.75f);
		}

		if (m_entity->GetSkeleton()->HasBone("Hip_L"))
		{
			m_weaponEntity = m_scene.CreateEntity(m_entity->GetName() + "_WEAPON", "Models/Character/Items/Weapons/Sword_1H_Newbie_01.hmsh");
			m_weaponAttachment = m_entity->AttachObjectToBone("Hip_L", *m_weaponEntity);
			m_weaponAttachment->SetScale(Vector3::UnitScale * 1.0f);
		}
		*/
	}
}
