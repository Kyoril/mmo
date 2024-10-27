#pragma once

#include "connection.h"
#include "db_cache.h"
#include "base/typedefs.h"
#include "base/signal.h"
#include "game/item.h"
#include "game_protocol/game_incoming_packet.h"
#include "net/realm_connector.h"

namespace mmo
{
	class GameObjectC;

	class LootClient final
	{
	public:
		LootClient(RealmConnector& connector, DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& itemCache)
			: m_realmConnector(connector)
			, m_itemCache(itemCache)
		{
		}
		~LootClient() = default;

	public:
		void Initialize();

		void Shutdown();

	public:
		void LootObject(const GameObjectC& object);

		void CloseLoot();

		uint32 GetNumLootItems() const;

		bool HasMoney() const;

	private:
		PacketParseResult OnLootResponse(game::IncomingPacket& packet);

		PacketParseResult OnLootReleaseResponse(game::IncomingPacket& packet);

		PacketParseResult OnLootRemoved(game::IncomingPacket& packet);

		PacketParseResult OnLootMoneyNotify(game::IncomingPacket& packet);

		PacketParseResult OnLootItemNotify(game::IncomingPacket& packet);

		PacketParseResult OnLootClearMoney(game::IncomingPacket& packet);

	private:
		RealmConnector& m_realmConnector;
		DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& m_itemCache;
		uint64 m_requestedLootObject = 0;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerConnections;

		uint32 m_lootMoney = 0;

		struct LootItem
		{
			uint8 slot;
			uint32 itemId;
			uint32 displayId;
			uint32 count;
			uint8 lootType;
		};

		std::vector<LootItem> m_lootItems;
	};
}
