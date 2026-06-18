// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game_protocol/game_protocol.h"

#include <array>
#include <string>

namespace mmo
{
	class Player;
	class GameItemS;

	/// @brief Number of item slots available per player in a trade session.
	static constexpr uint8 TradeSlotCount = 6;

	/// @brief Holds one player's side of a trade offer.
	struct TradeOffer
	{
		/// Absolute inventory slot for each trade slot (0 = empty).
		std::array<uint16, TradeSlotCount> itemSlots{};

		/// Money offered in copper.
		uint32 money{ 0 };

		/// Whether this player has accepted the current trade terms.
		bool accepted{ false };
	};

	/// @brief Manages the server-side state of an active trade between two players.
	///
	/// Each Player holds a shared_ptr to the same TradeSession. When a player
	/// adds/removes items or accepts, the session updates and notifies both players.
	class TradeSession final
	{
	public:
		/// @brief Constructs a trade session between two players.
		/// @param initiator The player who initiated the trade (index 0).
		/// @param target The player who accepted the trade invitation (index 1).
		TradeSession(Player& initiator, Player& target);
		~TradeSession() = default;

		TradeSession(const TradeSession&) = delete;
		TradeSession& operator=(const TradeSession&) = delete;

	public:
		/// @brief Returns the player at the given internal index (0 or 1).
		[[nodiscard]] Player& GetPlayer(int index) const;

		/// @brief Returns the offer for the given player index.
		[[nodiscard]] const TradeOffer& GetOffer(int index) const;

		/// @brief Returns the index (0 or 1) for the given player, or -1 if not in this session.
		[[nodiscard]] int GetPlayerIndex(const Player& player) const;

		/// @brief Returns the index of the other player.
		[[nodiscard]] int GetOtherIndex(int myIndex) const { return 1 - myIndex; }

		/// @brief Adds an inventory item to a trade slot for the given player.
		/// @param playerIndex 0 or 1.
		/// @param tradeSlot 0-5.
		/// @param inventorySlot Absolute inventory slot of the item.
		/// @return true if the slot was updated.
		bool AddItem(int playerIndex, uint8 tradeSlot, uint16 inventorySlot);

		/// @brief Removes an item from a trade slot for the given player.
		/// @param playerIndex 0 or 1.
		/// @param tradeSlot 0-5.
		/// @return true if the slot was cleared.
		bool RemoveItem(int playerIndex, uint8 tradeSlot);

		/// @brief Sets the money offered by the given player.
		/// @param playerIndex 0 or 1.
		/// @param amount Money in copper.
		void SetMoney(int playerIndex, uint32 amount);

		/// @brief Marks the given player as having accepted the trade.
		/// @param playerIndex 0 or 1.
		/// @return true if both players have now accepted (trade should execute).
		bool Accept(int playerIndex);

		/// @brief Resets acceptance flags (called when any offer changes).
		void InvalidateAcceptance();

		/// @brief Returns true if the given inventory slot is locked by this session for the given player.
		[[nodiscard]] bool IsSlotLocked(int playerIndex, uint16 inventorySlot) const;

		/// @brief Returns whether the trade session is still valid (both players alive, in range, etc.).
		[[nodiscard]] bool IsValid() const;

		/// @brief Executes the trade: moves items and money between both players.
		/// @return true on full success, false if anything failed (caller must cancel).
		bool Execute();

		/// @brief Sends a TradeUpdate packet to the specified player containing the specified offer.
		/// @param recipientIndex Which player (0 or 1) receives the packet.
		/// @param offerIndex Which offer (0 or 1) to send. Matching recipientIndex means "self" offer.
		void SendUpdateToPlayer(int recipientIndex, int offerIndex) const;

		/// @brief Sends TradeAcceptUpdate to both players.
		void SendAcceptUpdateToBoth() const;

	private:
		/// Clears acceptance and notifies both players.
		void NotifyOfferChanged();

	private:
		Player* m_players[2];
		TradeOffer m_offers[2];
	};
}
