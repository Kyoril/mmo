
#include "loot_client.h"
#include "game_client/game_object_c.h"
#include "net/realm_connector.h"
#include "frame_ui/frame_mgr.h"
#include "game/loot.h"

namespace mmo
{
	void LootClient::Initialize()
	{
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::LootResponse, *this, &LootClient::OnLootResponse);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::LootReleaseResponse, *this, &LootClient::OnLootReleaseResponse);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::LootMoneyNotify, *this, &LootClient::OnLootMoneyNotify);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::LootClearMoney, *this, &LootClient::OnLootClearMoney);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::LootRemoved, *this, &LootClient::OnLootRemoved);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::LootItemNotify, *this, &LootClient::OnLootItemNotify);
	}

	void LootClient::Shutdown()
	{
		m_packetHandlerConnections.Clear();
		m_requestedLootObject = 0;
	}

	PacketParseResult LootClient::OnLootResponse(game::IncomingPacket& packet)
	{
		uint64 objectGuid;
		uint8 lootType;
		if (!(packet >> io::read<uint64>(objectGuid) >> io::read<uint8>(lootType)))
		{
			return PacketParseResult::Disconnect;
		}

		// Switch looted object
		if (objectGuid != m_requestedLootObject)
		{
			m_realmConnector.LootRelease(m_requestedLootObject);
			m_requestedLootObject = objectGuid;
		}

		// Loot error?
		if (lootType == loot_type::None)
		{
			uint8 lootError;
			if (!(packet >> io::read<uint8>(lootError)))
			{
				return PacketParseResult::Disconnect;
			}

			// TODO: Display game error
			ELOG("Failed to loot object " << log_hex_digit(objectGuid) << ": " << log_hex_digit(lootError));
			return PacketParseResult::Pass;
		}

		// No loot error, so parse the actual loot
		uint8 itemCount;
		if (!(packet >> io::read<uint32>(m_lootMoney) >> io::read<uint8>(itemCount)))
		{
			return PacketParseResult::Disconnect;
		}

		m_lootItems.clear();
		m_lootItems.reserve(12);

		for (uint8 i = 0; i < itemCount; ++i)
		{
			LootItem item;
			if (!(packet >> io::read<uint8>(item.slot) >> io::read<uint32>(item.itemId) >> io::read<uint32>(item.count) >> io::read<uint32>(item.displayId) >> io::skip<uint32>() >> io::skip<uint32>() >> io::read<uint8>(item.lootType)))
			{
				return PacketParseResult::Disconnect;
			}
			m_lootItems.emplace_back(std::move(item));
		}

		DLOG("Loot received - Gold: " << m_lootMoney << "; Items: " << static_cast<uint16>(itemCount));

		// Notify the loot frame manager
		FrameManager::Get().TriggerLuaEvent("LOOT_OPENED");

		return PacketParseResult::Pass;
	}

	PacketParseResult LootClient::OnLootReleaseResponse(game::IncomingPacket& packet)
	{
		DLOG("Received SMSG_LOOT_RELEASE_RESPONSE");

		m_lootMoney = 0;
		m_lootItems.clear();
		m_lootItems.reserve(12);
		m_requestedLootObject = 0;

		FrameManager::Get().TriggerLuaEvent("LOOT_CLOSED");

		return PacketParseResult::Pass;
	}

	PacketParseResult LootClient::OnLootRemoved(game::IncomingPacket& packet)
	{
		DLOG("Received SMSG_LOOT_REMOVED");

		// TODO: UI events

		return PacketParseResult::Pass;
	}

	PacketParseResult LootClient::OnLootMoneyNotify(game::IncomingPacket& packet)
	{
		DLOG("Received SMSG_LOOT_MONEY_NOTIFY");

		// TODO: UI events

		return PacketParseResult::Pass;
	}

	PacketParseResult LootClient::OnLootItemNotify(game::IncomingPacket& packet)
	{
		DLOG("Received SMSG_LOOT_ITEM_NOTIFY");

		// TODO: UI events

		return PacketParseResult::Pass;
	}

	PacketParseResult LootClient::OnLootClearMoney(game::IncomingPacket& packet)
	{
		DLOG("Received SMSG_LOOT_CLEAR_MONEY");

		m_lootMoney = 0;

		// TODO: UI events?

		return PacketParseResult::Pass;
	}

	void LootClient::LootObject(const GameObjectC& object)
	{
		// We request loot from the client
		m_requestedLootObject = object.GetGuid();
		m_realmConnector.Loot(m_requestedLootObject);
	}

	void LootClient::CloseLoot()
	{
		ASSERT(m_requestedLootObject != 0);

		m_realmConnector.LootRelease(m_requestedLootObject);
		m_requestedLootObject = 0;
	}

	uint32 LootClient::GetNumLootItems() const
	{
		uint32 count = static_cast<uint32>(m_lootItems.size());

		if (m_lootMoney > 0)
		{
			++count;
		}

		return count;
	}

	bool LootClient::HasMoney() const
	{
		return m_lootMoney > 0;
	}
}
