
#include "vendor_client.h"

#include "game/vendor.h"

namespace mmo
{
	VendorClient::VendorClient(RealmConnector& connector)
		: m_realmConnector(connector)
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

			return PacketParseResult::Pass;
		}

		DLOG("Received vendor inventory list with " << static_cast<int32>(listCount) << " items!");

		// TODO: Read item list

		return PacketParseResult::Pass;
	}
}
