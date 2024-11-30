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

	public:
		VendorClient(RealmConnector& connector, DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& itemCache);
		~VendorClient();

	public:
		void Initialize();

		void Shutdown();

	public:
		bool HasVendor() const { return m_vendorGuid != 0; }

		uint64 GetVendorGuid() const { return m_vendorGuid; }

		void SellItem(uint64 itemGuid) const;

		void BuyItem(uint32 index, uint8 count) const;

		void CloseVendor();

		uint32 GetNumVendorItems() const;

		const std::vector<VendorItemEntry>& GetVendorItems() const { return m_vendorItems; }

	private:
		PacketParseResult OnListInventory(game::IncomingPacket& packet);

	private:
		RealmConnector& m_realmConnector;
		DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& m_itemCache;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerConnections;

		std::vector<VendorItemEntry> m_vendorItems;
		uint32 m_vendorPendingRequestCount = 0;
		uint64 m_vendorGuid = 0;
	};
}
