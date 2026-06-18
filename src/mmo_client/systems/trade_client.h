// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "net/realm_connector.h"

#include <array>
#include <string>

struct lua_State;

namespace mmo
{
	/// @brief Client-side representation of one slot in a trade offer.
	struct TradeSlotInfo
	{
		uint32 itemEntry{ 0 };
		uint32 itemCount{ 0 };
	};

	static constexpr uint8 ClientTradeSlotCount = 6;

	/// @brief Client-side trade system.
	///
	/// Manages incoming server packets for trade events, fires Lua events,
	/// and provides Lua-callable functions for trade operations.
	class TradeClient final : public NonCopyable
	{
	public:
		explicit TradeClient(RealmConnector& connector);

	public:
		void Initialize();
		void Shutdown();

		/// @brief Registers Lua API functions.
		void RegisterScriptFunctions(lua_State* lua);

	public:
		/// Returns item info for a trade slot.  @param slot 0-5, @param isMine true for our offer.
		const TradeSlotInfo* GetTradeSlotInfo(uint8 slot, bool isMine) const;

		/// Returns the money offered. @param isMine true for our offer.
		uint32 GetTradeMoney(bool isMine) const;

		/// Returns true if there is an active trade session.
		bool IsTrading() const { return m_inSession; }

	private:
		PacketParseResult OnTradeInvite(game::IncomingPacket& packet);
		PacketParseResult OnTradeRequestResult(game::IncomingPacket& packet);
		PacketParseResult OnTradeSessionOpened(game::IncomingPacket& packet);
		PacketParseResult OnTradeSessionClosed(game::IncomingPacket& packet);
		PacketParseResult OnTradeUpdate(game::IncomingPacket& packet);
		PacketParseResult OnTradeAcceptUpdate(game::IncomingPacket& packet);

	private:
		RealmConnector& m_connector;
		RealmConnector::PacketHandlerHandleContainer m_handlers;

		bool m_inSession{ false };
		std::string m_otherPlayerName;
		uint64 m_otherPlayerGuid{ 0 };

		/// Offer from other player (updated via TradeUpdate from server).
		std::array<TradeSlotInfo, ClientTradeSlotCount> m_otherOffer{};
		uint32 m_otherMoney{ 0 };

		/// Our own offer (tracked locally for display purposes).
		std::array<TradeSlotInfo, ClientTradeSlotCount> m_myOffer{};
		uint32 m_myMoney{ 0 };

		/// Acceptance state.
		bool m_myAccepted{ false };
		bool m_otherAccepted{ false };

		/// Name of the player who sent a pending trade invitation.
		std::string m_pendingInviterName;
		uint64 m_pendingInviterGuid{ 0 };
	};
}
