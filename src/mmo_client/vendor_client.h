#pragma once

#include "db_cache.h"
#include "base/non_copyable.h"
#include "game/item.h"
#include "net/realm_connector.h"

namespace mmo
{
	class VendorClient final : public NonCopyable
	{
	public:
		VendorClient(RealmConnector& connector, DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& itemCache);
		~VendorClient();

	public:
		void Initialize();

		void Shutdown();

	private:
		uint32 GetNumVendorItems() const;

	private:
		PacketParseResult OnListInventory(game::IncomingPacket& packet);

	private:
		RealmConnector& m_realmConnector;
		DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& m_itemCache;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerConnections;

		struct VendorItemEntry
		{
			uint32 index;
			uint32 itemId;
			uint32 displayId;
			uint32 maxCount;
			uint32 buyPrice;
			uint32 durability;
			uint32 buyCount;
			uint32 extendedCost;
			const ItemInfo* itemData = nullptr;
		};

		std::vector<VendorItemEntry> m_vendorItems;
		uint32 m_vendorPendingRequestCount = 0;
		uint64 m_vendorGuid = 0;
	};
}
