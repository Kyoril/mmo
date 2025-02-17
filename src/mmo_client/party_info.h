#pragma once
#include "client_cache.h"
#include "base/non_copyable.h"
#include "game/group.h"
#include "game/spell.h"
#include "net/realm_connector.h"

namespace mmo
{
	struct PartyMember
	{
		uint64 guid;
		String name;
		uint8 group;
		bool assistant;
		uint32 status;

		uint32 level = 1;
		uint32 health = 0;
		uint32 maxHealth = 1;
		uint32 powerType = power_type::Mana;
		uint32 power = 0;
		uint32 maxPower = 1;
	};

	class PartyInfo final : public NonCopyable
	{
	public:
		explicit PartyInfo(RealmConnector& realmConnector, DBNameCache& nameCache);

	public:
		void Initialize();

		void Shutdown();

		uint32 GetMemberCount() const { return m_members.size(); }

		uint64 GetMemberGuid(int32 index) const;

		uint64 GetLeaderGuid() const;

		uint64 GetLootMasterGuid() const;

		LootMethod GetLootMethod() const;

		GroupType GetGroupType() const;

		bool IsAssistant() const;

		uint8 GetLootThreshold() const { return m_lootThreshold; }

		const PartyMember* GetMember(int32 index) const;

	private:
		PacketParseResult OnGroupDestroyed(game::IncomingPacket& packet);
		PacketParseResult OnGroupList(game::IncomingPacket& packet);

	private:
		RealmConnector& m_realmConnector;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerHandles;
		DBNameCache& m_nameCache;

		GroupType m_type;
		uint64 m_leaderGuid = 0;
		uint64 m_lootMaster = 0;
		LootMethod m_lootMethod = loot_method::GroupLoot;
		bool m_assistant = false;
		std::vector<PartyMember> m_members;
		uint8 m_lootThreshold = 0;
	};
}
