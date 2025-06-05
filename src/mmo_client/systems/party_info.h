#pragma once

#include "mmo_client/data/client_cache.h"
#include "base/non_copyable.h"
#include "game/group.h"
#include "game/spell.h"
#include "game_client/game_player_c.h"
#include "mmo_client/net/realm_connector.h"

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

		bool IsGroupMember(uint64 memberGuid) const;

		int32 GetMemberIndexByGuid(uint64 memberGuid) const;

		uint32 GetMemberCount() const { return m_members.size(); }

		uint64 GetMemberGuid(int32 index) const;

		uint64 GetLeaderGuid() const;

		uint64 GetLootMasterGuid() const;

		int32 GetLeaderIndex() const;

		LootMethod GetLootMethod() const;

		GroupType GetGroupType() const;

		bool IsAssistant() const;

		uint8 GetLootThreshold() const { return m_lootThreshold; }

		const PartyMember* GetMember(int32 index) const;

		void OnPlayerSpawned(GamePlayerC& player);

		void OnPlayerDespawned(uint64 guid);

	private:
		PacketParseResult OnGroupDestroyed(game::IncomingPacket& packet);
		PacketParseResult OnGroupList(game::IncomingPacket& packet);
		PacketParseResult OnPartyMemberStats(game::IncomingPacket& packet);
		PacketParseResult OnGroupSetLeader(game::IncomingPacket& packet);

	private:
		void RegisterPlayerMirrorHandlers(GamePlayerC& player);

		void OnMemberHealthChanged(uint64 monitoredGuid);

		void OnMemberPowerChanged(uint64 monitoredGuid);

		void OnMemberLevelChanged(uint64 monitoredGuid);

		template<class Callback>
		void ForMemberIndex(uint64 guid, const Callback& callback)
		{
			for (int32 index = 0; index < m_members.size(); ++index)
			{
				if (m_members[index].guid == guid)
				{
					callback(index);
					return;
				}
			}
		}
		

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
		std::map<uint64, scoped_connection_container> m_memberObservers;
	};
}
