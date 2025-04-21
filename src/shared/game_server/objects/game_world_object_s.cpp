
#include "game_world_object_s.h"

#include "game_player_s.h"
#include "shared/proto_data/objects.pb.h"

namespace mmo
{
	GameWorldObjectS::GameWorldObjectS(const proto::Project& project, const proto::ObjectEntry& entry)
		: GameObjectS(project)
		, m_entry(entry)
	{
	}

	void GameWorldObjectS::Initialize()
	{
		GameObjectS::Initialize();

		Set<uint32>(object_fields::Entry, m_entry.id());
		Set<uint32>(object_fields::ObjectDisplayId, m_entry.displayid());
		Set<uint32>(object_fields::ObjectTypeId, static_cast<uint32>(GameWorldObjectType::Chest));	// TODO
	}

	bool GameWorldObjectS::IsUsable() const
	{
		// TODO
		return true;
	}

	void GameWorldObjectS::Use(GamePlayerS& player)
	{
		if (!m_loot)
		{
			const auto weakPlayer = std::weak_ptr(std::dynamic_pointer_cast<GamePlayerS>(player.shared_from_this()));
			m_loot = std::make_shared<LootInstance>(m_project.items, GetGuid(), m_project.unitLoot.getById(m_entry.objectlootentry()), 0, 0, std::vector{ weakPlayer });

			auto weakThis = std::weak_ptr(shared_from_this());
			m_lootSignals += m_loot->closed.connect(this, &GameWorldObjectS::OnLootClosed);
			m_lootSignals += m_loot->cleared.connect(this, &GameWorldObjectS::OnLootCleared);
		}

		player.LootObject(shared_from_this());
	}

	const String& GameWorldObjectS::GetName() const
	{
		return m_entry.name();
	}

	void GameWorldObjectS::PrepareFieldMap()
	{
		m_fields.Initialize(object_fields::WorldObjectFieldCount);
	}

	void GameWorldObjectS::OnLootClosed(uint64 lootGuid)
	{
		// TODO
	}

	void GameWorldObjectS::OnLootCleared()
	{
		// TODO: should we really despawn the entire object?
		Despawn();
	}
}
