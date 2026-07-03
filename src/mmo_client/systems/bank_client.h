// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "net/realm_connector.h"

struct lua_State;

namespace mmo
{
	/// Client side system which handles the bank window interaction with a banker npc.
	class BankClient final : public NonCopyable
	{
	public:
		explicit BankClient(RealmConnector& connector);
		~BankClient();

	public:
		void Initialize();

		void Shutdown();

		void RegisterScriptFunctions(lua_State* lua);

	public:
		bool HasBank() const { return m_bankerGuid != 0; }

		uint64 GetBankerGuid() const { return m_bankerGuid; }

		/// Requests the purchase of the next bank bag slot from the current banker.
		void BuyBankBagSlot() const;

		void CloseBank();

		/// Gets the number of bank bag slots the player has purchased.
		uint32 GetNumBankBagSlots() const;

		/// Gets the price in copper of the next purchasable bank bag slot, or 0 if all slots are purchased.
		uint32 GetNextBankBagSlotPrice() const;

	private:
		PacketParseResult OnShowBank(game::IncomingPacket& packet);

		PacketParseResult OnBuyBankBagSlotResult(game::IncomingPacket& packet);

	private:
		RealmConnector& m_realmConnector;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerConnections;

		uint64 m_bankerGuid = 0;

		/// Slot count as last confirmed by the server through a purchase result. The player field
		/// update can arrive after the purchase result packet, so this cache keeps the UI in sync.
		/// Negative means no purchase happened yet and the player field is authoritative.
		int32 m_confirmedBagSlotCount = -1;
	};
}
