
#include "vendor_client.h"

#include "frame_ui/frame_mgr.h"
#include "game/vendor.h"

namespace mmo
{
	VendorClient::VendorClient(RealmConnector& connector, DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& itemCache)
		: m_realmConnector(connector)
		, m_itemCache(itemCache)
	{
	}

	VendorClient::~VendorClient()
		= default;

	void VendorClient::Initialize()
	{
		ASSERT(m_packetHandlerConnections.IsEmpty());

		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ListInventory, *this, &VendorClient::OnListInventory);
	}

	void VendorClient::Shutdown()
	{
		m_packetHandlerConnections.Clear();
	}

	uint32 VendorClient::GetNumVendorItems() const
	{
		// TODO
		return 0;
	}

	PacketParseResult VendorClient::OnListInventory(game::IncomingPacket& packet)
	{
		uint64 vendorGuid;
		uint8 listCount;

		if (!(packet >> io::read<uint64>(vendorGuid) >> io::read<uint8>(listCount)))
		{
			ELOG("Failed to read InventoryList packet!");
			return PacketParseResult::Disconnect;
		}

		// Check for error packet
		if (listCount == 0)
		{
			vendor_result::Type result;
			if (!(packet >> io::read<uint8>(result)))
			{
				ELOG("Failed to read error result from inventory list packet!");
				return PacketParseResult::Disconnect;
			}

			switch (result)
			{
			case vendor_result::VendorHasNoItems:
				ELOG("Vendor has no items!");
				break;
			case vendor_result::CantShopWhileDead:
				ELOG("You can't shop while you are dead!");
				break;
			case vendor_result::VendorTooFarAway:
				ELOG("The vendor is too far away!");
				break;
			case vendor_result::VendorHostile:
				ELOG("That vendor does not like you!");
				break;
			case vendor_result::VendorIsDead:
				ELOG("Vendor is dead!");
				break;
			}

			m_vendorGuid = 0;
			FrameManager::Get().TriggerLuaEvent("VENDOR_CLOSED");

			return PacketParseResult::Pass;
		}

		DLOG("Received vendor inventory list with " << static_cast<int32>(listCount) << " items!");

		// Clear vendor item count
		m_vendorItems.clear();
		m_vendorItems.reserve(listCount);
		m_vendorGuid = vendorGuid;
		m_realmConnector.SetSelection(m_vendorGuid);

		for (uint8 i = 0; i < listCount; ++i)
		{
			VendorItemEntry entry;
			if (!(packet 
				>> io::read<uint32>(entry.index)
				>> io::read<uint32>(entry.itemId) 
				>> io::read<uint32>(entry.displayId) 
				>> io::read<uint32>(entry.maxCount) 
				>> io::read<uint32>(entry.buyPrice)
				>> io::read<uint32>(entry.durability) 
				>> io::read<uint32>(entry.buyCount) 
				>> io::read<uint32>(entry.extendedCost)))
			{
				ELOG("Failed to read vendor item entry!");
				return PacketParseResult::Disconnect;
			}

			// Check if the item is a valid item
			m_vendorPendingRequestCount++;
			m_vendorItems.push_back(entry);

			m_itemCache.Get(entry.itemId, [&](uint64 id, const ItemInfo& itemInfo)
				{
					for (auto& lootItem : m_vendorItems)
					{
						if (lootItem.itemId == id)
						{
							lootItem.itemData = &itemInfo;
						}
					}

					if (m_vendorPendingRequestCount == 1 && m_vendorGuid != 0)
					{
						m_vendorPendingRequestCount = 0;

						// Notify the loot frame manager
						FrameManager::Get().TriggerLuaEvent("VENDOR_SHOW");
					}
					else
					{
						m_vendorPendingRequestCount--;
					}
				});
		}

		return PacketParseResult::Pass;
	}
}
