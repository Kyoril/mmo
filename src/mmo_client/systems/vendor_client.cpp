#include "vendor_client.h"

#include "frame_ui/frame_mgr.h"
#include "game/vendor.h"
#include "game_client/object_mgr.h"
#include "unit_gossip_voice.h"

#include "luabind_lambda.h"

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
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::BuyBackList, *this, &VendorClient::OnBuyBackList);
	}

	void VendorClient::Shutdown()
	{
		m_packetHandlerConnections.Clear();
	}

	void VendorClient::RegisterScriptFunctions(lua_State* lua)
	{
		LUABIND_MODULE(lua,
			luabind::def_lambda("GetVendorNumItems", [this]() { return GetNumVendorItems(); }),
			luabind::def_lambda("GetVendorNumBuybackItems", [this]() { return GetNumBuyBackItems(); }),
			luabind::def_lambda("BuybackVendorItem", [this](uint32 index) { BuyBackItem(index); }),
			luabind::def_lambda("CloseVendor", [this]() { CloseVendor(); })
		);
	}

	void VendorClient::SellItem(uint64 itemGuid) const
	{
		ASSERT(itemGuid != 0);

		if (!m_vendorGuid)
		{
			ELOG("No vendor available right now!");
			return;
		}

		m_realmConnector.SellItem(m_vendorGuid, itemGuid);
	}

	void VendorClient::BuyItem(uint32 index, uint8 count) const
	{
		if (!m_vendorGuid)
		{
			ELOG("No vendor available right now!");
			return;
		}

		if (index >= m_vendorItems.size())
		{
			ELOG("Invalid index to buy from!");
			return;
		}

		m_realmConnector.BuyItem(m_vendorGuid, m_vendorItems[index].itemId, count);
	}

	void VendorClient::BuyBackItem(uint32 index) const
	{
		if (!m_vendorGuid)
		{
			ELOG("No vendor available right now!");
			return;
		}

		if (index >= m_buyBackItems.size())
		{
			ELOG("Invalid index to buy back from!");
			return;
		}

		// The server addresses buyback offers by their slot, which is stable while the vendor is
		// open, unlike the index into the condensed list shown in the ui.
		m_realmConnector.BuyBackItem(m_vendorGuid, m_buyBackItems[index].slot);
	}

	void VendorClient::CloseVendor()
	{
		if (m_vendorGuid == 0)
		{
			return;
		}

		UnitGossipVoice::Get().OnNpcDialogClosed(npc_dialog_source::Vendor, m_vendorGuid);

		m_vendorGuid = 0;
		m_vendorItems.clear();
		m_buyBackItems.clear();

		FrameManager::Get().TriggerLuaEvent("VENDOR_CLOSED");
	}

	uint32 VendorClient::GetNumVendorItems() const
	{
		return m_vendorItems.size();
	}

	uint32 VendorClient::GetNumBuyBackItems() const
	{
		return m_buyBackItems.size();
	}

	PacketParseResult VendorClient::OnBuyBackList(game::IncomingPacket& packet)
	{
		uint64 vendorGuid;
		uint8 listCount;

		if (!(packet >> io::read<uint64>(vendorGuid) >> io::read<uint8>(listCount)))
		{
			ELOG("Failed to read BuyBackList packet!");
			return PacketParseResult::Disconnect;
		}

		// A list that arrived after the vendor window was closed (or for another npc) is stale.
		if (m_vendorGuid == 0 || m_vendorGuid != vendorGuid)
		{
			return PacketParseResult::Pass;
		}

		m_buyBackItems.clear();
		m_buyBackItems.reserve(listCount);

		for (uint8 i = 0; i < listCount; ++i)
		{
			VendorBuyBackEntry entry;
			if (!(packet
				>> io::read<uint32>(entry.slot)
				>> io::read<uint32>(entry.itemId)
				>> io::read<uint32>(entry.displayId)
				>> io::read<uint32>(entry.stackCount)
				>> io::read<uint32>(entry.price)
				>> io::read<uint32>(entry.durability)))
			{
				ELOG("Failed to read buyback item entry!");
				return PacketParseResult::Disconnect;
			}

			m_buyBackItems.push_back(entry);
		}

		// The item data may still have to be fetched, so the ui is refreshed again for every entry
		// that arrives. The buyback list is short, so this stays cheap.
		for (const auto& entry : m_buyBackItems)
		{
			m_itemCache.Get(entry.itemId, [this](uint64 id, const ItemInfo& itemInfo)
				{
					for (auto& buyBackItem : m_buyBackItems)
					{
						if (buyBackItem.itemId == id)
						{
							buyBackItem.itemData = &itemInfo;
						}
					}

					FrameManager::Get().TriggerLuaEvent("VENDOR_BUYBACK_UPDATE");
				});
		}

		FrameManager::Get().TriggerLuaEvent("VENDOR_BUYBACK_UPDATE");

		return PacketParseResult::Pass;
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

			if (m_vendorGuid != 0)
			{
				CloseVendor();
			}
			else
			{
				FrameManager::Get().TriggerLuaEvent("VENDOR_CLOSED");
			}

			return PacketParseResult::Pass;
		}

		DLOG("Received vendor inventory list with " << static_cast<int32>(listCount) << " items!");

		// Clear vendor item count
		m_vendorItems.clear();
		m_vendorItems.reserve(listCount);
		m_vendorGuid = vendorGuid;
		UnitGossipVoice::Get().OnNpcDialogOpened(npc_dialog_source::Vendor, m_vendorGuid);

		auto player = ObjectMgr::GetActivePlayer();
		ASSERT(player);
		player->SetTargetUnit(ObjectMgr::Get<GameUnitC>(m_vendorGuid));

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
