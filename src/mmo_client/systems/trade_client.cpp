// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "trade_client.h"

#include "frame_ui/frame_mgr.h"
#include "log/default_log_levels.h"
#include "luabind_lambda.h"

namespace mmo
{
	TradeClient::TradeClient(RealmConnector& connector)
		: m_connector(connector)
	{
	}

	void TradeClient::Initialize()
	{
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::TradeInvite, *this, &TradeClient::OnTradeInvite);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::TradeRequestResult, *this, &TradeClient::OnTradeRequestResult);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::TradeSessionOpened, *this, &TradeClient::OnTradeSessionOpened);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::TradeSessionClosed, *this, &TradeClient::OnTradeSessionClosed);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::TradeUpdate, *this, &TradeClient::OnTradeUpdate);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::TradeAcceptUpdate, *this, &TradeClient::OnTradeAcceptUpdate);
	}

	void TradeClient::Shutdown()
	{
		m_handlers.Clear();
	}

	void TradeClient::RegisterScriptFunctions(lua_State* lua)
	{
		LUABIND_MODULE(lua,
			luabind::def_lambda("InitiateTrade", [this](const uint64 targetGuid)
			{
				m_connector.InitiateTrade(targetGuid);
			}),
			luabind::def_lambda("AcceptTradeInvite", [this]()
			{
				m_connector.AcceptTradeInvite();
			}),
			luabind::def_lambda("DeclineTradeInvite", [this]()
			{
				m_connector.DeclineTradeInvite();
			}),
			luabind::def_lambda("CancelTrade", [this]()
			{
				m_connector.CancelTrade();
				m_inSession = false;
				m_myOffer.fill({});
				m_otherOffer.fill({});
				m_myMoney = 0;
				m_otherMoney = 0;
				m_myAccepted = false;
				m_otherAccepted = false;
			}),
			luabind::def_lambda("TradeAddItem", [this](const uint8 tradeSlot, const uint16 inventorySlot)
			{
				m_connector.TradeAddItem(tradeSlot, inventorySlot);
			}),
			luabind::def_lambda("ClearTradeItem", [this](const uint8 tradeSlot)
			{
				m_connector.TradeRemoveItem(tradeSlot);
				if (tradeSlot < ClientTradeSlotCount)
				{
					m_myOffer[tradeSlot] = {};
				}
			}),
			luabind::def_lambda("SetTradeMoney", [this](const uint32 amount)
			{
				m_connector.TradeSetMoney(amount);
				m_myMoney = amount;
			}),
			luabind::def_lambda("AcceptTrade", [this]()
			{
				m_connector.AcceptTrade();
			}),
			luabind::def_lambda("IsTrading", [this]()
			{
				return IsTrading();
			}),
			luabind::def_lambda("GetTradeItemEntry",
				[this](const uint8 slotIndex, const bool isMine) -> uint32
				{
					const TradeSlotInfo* info = GetTradeSlotInfo(slotIndex, isMine);
					return (info != nullptr) ? info->itemEntry : 0u;
				}),
			luabind::def_lambda("GetTradeItemCount",
				[this](const uint8 slotIndex, const bool isMine) -> uint32
				{
					const TradeSlotInfo* info = GetTradeSlotInfo(slotIndex, isMine);
					return (info != nullptr) ? info->itemCount : 0u;
				}),
			luabind::def_lambda("GetTradeMoney", [this](const bool isMine)
			{
				return GetTradeMoney(isMine);
			})
		);
	}

	const TradeSlotInfo* TradeClient::GetTradeSlotInfo(const uint8 slot, const bool isMine) const
	{
		if (slot >= ClientTradeSlotCount)
		{
			return nullptr;
		}
		return isMine ? &m_myOffer[slot] : &m_otherOffer[slot];
	}

	uint32 TradeClient::GetTradeMoney(const bool isMine) const
	{
		return isMine ? m_myMoney : m_otherMoney;
	}

	PacketParseResult TradeClient::OnTradeInvite(game::IncomingPacket& packet)
	{
		std::string inviterName;
		uint64 inviterGuid;
		if (!(packet >> io::read_string(inviterName) >> io::read<uint64>(inviterGuid)))
		{
			return PacketParseResult::Disconnect;
		}

		m_pendingInviterName = inviterName;
		m_pendingInviterGuid = inviterGuid;

		FrameManager::Get().TriggerLuaEvent("TRADE_REQUEST", inviterName);

		return PacketParseResult::Pass;
	}

	PacketParseResult TradeClient::OnTradeRequestResult(game::IncomingPacket& packet)
	{
		uint8 result;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		FrameManager::Get().TriggerLuaEvent("TRADE_REQUEST_RESULT", static_cast<int32>(result));

		return PacketParseResult::Pass;
	}

	PacketParseResult TradeClient::OnTradeSessionOpened(game::IncomingPacket& packet)
	{
		std::string otherName;
		uint64 otherGuid;
		if (!(packet >> io::read_string(otherName) >> io::read<uint64>(otherGuid)))
		{
			return PacketParseResult::Disconnect;
		}

		m_inSession = true;
		m_otherPlayerName = otherName;
		m_otherPlayerGuid = otherGuid;
		m_myOffer.fill({});
		m_otherOffer.fill({});
		m_myMoney = 0;
		m_otherMoney = 0;
		m_myAccepted = false;
		m_otherAccepted = false;
		m_pendingInviterName.clear();
		m_pendingInviterGuid = 0;

		FrameManager::Get().TriggerLuaEvent("TRADE_SESSION_OPENED", otherName);

		return PacketParseResult::Pass;
	}

	PacketParseResult TradeClient::OnTradeSessionClosed(game::IncomingPacket& packet)
	{
		uint8 reason;
		if (!(packet >> io::read<uint8>(reason)))
		{
			return PacketParseResult::Disconnect;
		}

		m_inSession = false;
		m_otherOffer.fill({});
		m_myOffer.fill({});
		m_otherMoney = 0;
		m_myMoney = 0;
		m_myAccepted = false;
		m_otherAccepted = false;

		FrameManager::Get().TriggerLuaEvent("TRADE_SESSION_CLOSED", static_cast<int32>(reason));

		return PacketParseResult::Pass;
	}

	PacketParseResult TradeClient::OnTradeUpdate(game::IncomingPacket& packet)
	{
		for (uint8 i = 0; i < ClientTradeSlotCount; ++i)
		{
			uint32 entry, count;
			if (!(packet >> io::read<uint32>(entry) >> io::read<uint32>(count)))
			{
				return PacketParseResult::Disconnect;
			}

			m_otherOffer[i].itemEntry = entry;
			m_otherOffer[i].itemCount = count;
		}

		uint32 money;
		if (!(packet >> io::read<uint32>(money)))
		{
			return PacketParseResult::Disconnect;
		}
		m_otherMoney = money;

		FrameManager::Get().TriggerLuaEvent("TRADE_UPDATE");

		return PacketParseResult::Pass;
	}

	PacketParseResult TradeClient::OnTradeAcceptUpdate(game::IncomingPacket& packet)
	{
		uint8 myAccepted, otherAccepted;
		if (!(packet >> io::read<uint8>(myAccepted) >> io::read<uint8>(otherAccepted)))
		{
			return PacketParseResult::Disconnect;
		}

		m_myAccepted = myAccepted != 0;
		m_otherAccepted = otherAccepted != 0;

		FrameManager::Get().TriggerLuaEvent("TRADE_ACCEPT_UPDATE", m_myAccepted, m_otherAccepted);

		return PacketParseResult::Pass;
	}
}
