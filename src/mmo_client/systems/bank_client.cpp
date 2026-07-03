// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bank_client.h"

#include "frame_ui/frame_mgr.h"
#include "game/bank.h"
#include "game/object_type_id.h"
#include "game_client/object_mgr.h"

#include "luabind_lambda.h"

namespace mmo
{
	BankClient::BankClient(RealmConnector& connector)
		: m_realmConnector(connector)
	{
	}

	BankClient::~BankClient()
		= default;

	void BankClient::Initialize()
	{
		ASSERT(m_packetHandlerConnections.IsEmpty());

		m_bankerGuid = 0;
		m_confirmedBagSlotCount = -1;

		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ShowBank, *this, &BankClient::OnShowBank);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::BuyBankBagSlotResult, *this, &BankClient::OnBuyBankBagSlotResult);
	}

	void BankClient::Shutdown()
	{
		m_packetHandlerConnections.Clear();
	}

	void BankClient::RegisterScriptFunctions(lua_State* lua)
	{
		LUABIND_MODULE(lua,
			luabind::def_lambda("CloseBank", [this]() { CloseBank(); }),
			luabind::def_lambda("BuyBankBagSlot", [this]() { BuyBankBagSlot(); }),
			luabind::def_lambda("GetNumBankBagSlots", [this]() { return GetNumBankBagSlots(); }),
			luabind::def_lambda("GetNextBankBagSlotPrice", [this]() { return GetNextBankBagSlotPrice(); })
		);
	}

	void BankClient::BuyBankBagSlot() const
	{
		if (!m_bankerGuid)
		{
			ELOG("No banker available right now!");
			return;
		}

		m_realmConnector.BuyBankBagSlot(m_bankerGuid);
	}

	void BankClient::CloseBank()
	{
		if (m_bankerGuid == 0)
		{
			return;
		}

		m_bankerGuid = 0;

		FrameManager::Get().TriggerLuaEvent("BANK_CLOSED");
	}

	uint32 BankClient::GetNumBankBagSlots() const
	{
		// A purchase result is more up to date than the player field, which is
		// replicated through a separate object update packet.
		if (m_confirmedBagSlotCount >= 0)
		{
			return static_cast<uint32>(m_confirmedBagSlotCount);
		}

		const auto player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return 0;
		}

		return player->Get<uint32>(object_fields::BankBagSlotCount);
	}

	uint32 BankClient::GetNextBankBagSlotPrice() const
	{
		return GetBankBagSlotPrice(static_cast<uint8>(GetNumBankBagSlots()));
	}

	PacketParseResult BankClient::OnShowBank(game::IncomingPacket& packet)
	{
		uint64 bankerGuid;
		if (!(packet >> io::read<uint64>(bankerGuid)))
		{
			ELOG("Failed to read ShowBank packet!");
			return PacketParseResult::Disconnect;
		}

		m_bankerGuid = bankerGuid;

		const auto player = ObjectMgr::GetActivePlayer();
		ASSERT(player);
		player->SetTargetUnit(ObjectMgr::Get<GameUnitC>(m_bankerGuid));

		FrameManager::Get().TriggerLuaEvent("BANK_SHOW");

		return PacketParseResult::Pass;
	}

	PacketParseResult BankClient::OnBuyBankBagSlotResult(game::IncomingPacket& packet)
	{
		uint8 result;
		uint8 slotCount;
		if (!(packet >> io::read<uint8>(result) >> io::read<uint8>(slotCount)))
		{
			ELOG("Failed to read BuyBankBagSlotResult packet!");
			return PacketParseResult::Disconnect;
		}

		m_confirmedBagSlotCount = slotCount;

		switch (result)
		{
		case buy_bank_bag_slot_result::Ok:
			FrameManager::Get().TriggerLuaEvent("BANK_SLOTS_CHANGED");
			break;
		case buy_bank_bag_slot_result::FailedInsufficientFunds:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_BANK_SLOT_INSUFFICIENT_FUNDS");
			break;
		case buy_bank_bag_slot_result::FailedTooMany:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_BANK_SLOT_TOO_MANY");
			break;
		case buy_bank_bag_slot_result::FailedNotBanker:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_BANK_SLOT_NOT_BANKER");
			break;
		}

		return PacketParseResult::Pass;
	}
}
