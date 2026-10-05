// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "trade_session.h"
#include "player.h"

#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_item_s.h"
#include "game_server/inventory.h"
#include "game_server/inventory_types.h"
#include "game/object_type_id.h"
#include "log/default_log_levels.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"

#include <limits>
#include <map>

namespace mmo
{
	TradeSession::TradeSession(Player& initiator, Player& target)
	{
		m_players[0] = &initiator;
		m_players[1] = &target;
		m_offers[0] = {};
		m_offers[1] = {};
	}

	Player& TradeSession::GetPlayer(int index) const
	{
		ASSERT(index == 0 || index == 1);
		return *m_players[index];
	}

	const TradeOffer& TradeSession::GetOffer(int index) const
	{
		ASSERT(index == 0 || index == 1);
		return m_offers[index];
	}

	int TradeSession::GetPlayerIndex(const Player& player) const
	{
		if (m_players[0] == &player)
		{
			return 0;
		}

		if (m_players[1] == &player)
		{
			return 1;
		}

		return -1;
	}

	bool TradeSession::AddItem(int playerIndex, uint8 tradeSlot, uint16 inventorySlot)
	{
		ASSERT(playerIndex == 0 || playerIndex == 1);

		if (tradeSlot >= TradeSlotCount)
		{
			WLOG("Trade slot index out of range: " << static_cast<uint32>(tradeSlot));
			return false;
		}

		// Check if another trade slot already holds this inventory slot
		for (uint8 i = 0; i < TradeSlotCount; ++i)
		{
			if (i != tradeSlot && m_offers[playerIndex].itemSlots[i] == inventorySlot && inventorySlot != 0)
			{
				WLOG("Inventory slot already referenced in another trade slot");
				return false;
			}
		}

		uint64 itemGuid = 0;
		uint32 stackCount = 0;
		if (inventorySlot != 0)
		{
			const auto item = m_players[playerIndex]->GetCharacter().GetInventory().GetItemAtSlot(inventorySlot);
			if (!item)
			{
				WLOG("No item at offered inventory slot " << inventorySlot);
				return false;
			}

			itemGuid = item->GetGuid();
			stackCount = item->GetStackCount();
		}

		m_offers[playerIndex].itemSlots[tradeSlot] = inventorySlot;
		m_offers[playerIndex].itemGuids[tradeSlot] = itemGuid;
		m_offers[playerIndex].stackCounts[tradeSlot] = stackCount;
		NotifyOfferChanged();
		return true;
	}

	bool TradeSession::RemoveItem(int playerIndex, uint8 tradeSlot)
	{
		ASSERT(playerIndex == 0 || playerIndex == 1);

		if (tradeSlot >= TradeSlotCount)
		{
			WLOG("Trade slot index out of range: " << static_cast<uint32>(tradeSlot));
			return false;
		}

		m_offers[playerIndex].itemSlots[tradeSlot] = 0;
		m_offers[playerIndex].itemGuids[tradeSlot] = 0;
		m_offers[playerIndex].stackCounts[tradeSlot] = 0;
		NotifyOfferChanged();
		return true;
	}

	void TradeSession::SetMoney(int playerIndex, uint32 amount)
	{
		ASSERT(playerIndex == 0 || playerIndex == 1);
		m_offers[playerIndex].money = amount;
		NotifyOfferChanged();
	}

	bool TradeSession::Accept(int playerIndex)
	{
		ASSERT(playerIndex == 0 || playerIndex == 1);
		m_offers[playerIndex].accepted = true;
		SendAcceptUpdateToBoth();
		return m_offers[0].accepted && m_offers[1].accepted;
	}

	void TradeSession::InvalidateAcceptance()
	{
		m_offers[0].accepted = false;
		m_offers[1].accepted = false;
	}

	bool TradeSession::IsSlotLocked(int playerIndex, uint16 inventorySlot) const
	{
		ASSERT(playerIndex == 0 || playerIndex == 1);

		if (inventorySlot == 0)
		{
			return false;
		}

		for (uint8 i = 0; i < TradeSlotCount; ++i)
		{
			if (m_offers[playerIndex].itemSlots[i] == inventorySlot)
			{
				return true;
			}
		}

		return false;
	}

	bool TradeSession::IsValid() const
	{
		const GamePlayerS& char0 = m_players[0]->GetCharacter();
		const GamePlayerS& char1 = m_players[1]->GetCharacter();

		if (!char0.IsAlive() || !char1.IsAlive())
		{
			return false;
		}

		if (char0.IsInCombat() || char1.IsInCombat())
		{
			return false;
		}

		return true;
	}

	bool TradeSession::Execute()
	{
		GamePlayerS& char0 = m_players[0]->GetCharacter();
		GamePlayerS& char1 = m_players[1]->GetCharacter();

		Inventory& inv0 = char0.GetInventory();
		Inventory& inv1 = char1.GetInventory();

		// Validate money
		if (!char0.HasMoney(m_offers[0].money))
		{
			WLOG("Trade execute: player 0 does not have enough money");
			return false;
		}

		if (!char1.HasMoney(m_offers[1].money))
		{
			WLOG("Trade execute: player 1 does not have enough money");
			return false;
		}

		// Collect items to transfer and validate they still exist
		struct ItemTransfer
		{
			std::shared_ptr<GameItemS> item;
			uint16 srcSlot;
		};

		std::vector<ItemTransfer> transfers[2];

		for (int side = 0; side < 2; ++side)
		{
			const Inventory& inventory = (side == 0) ? inv0 : inv1;
			const TradeOffer& offer = m_offers[side];

			for (uint8 i = 0; i < TradeSlotCount; ++i)
			{
				if (offer.itemSlots[i] == 0)
				{
					continue;
				}

				// The partner accepted the item that was offered. If it was sold, destroyed or
				// split since, the slot now holds something else (or less of it) - abort rather
				// than hand over whatever is there.
				auto item = inventory.GetItemAtSlot(offer.itemSlots[i]);
				if (!item || item->GetGuid() != offer.itemGuids[i] || item->GetStackCount() != offer.stackCounts[i])
				{
					WLOG("Trade execute: item in player " << side << " trade slot " << static_cast<uint32>(i) << " changed since it was offered");
					return false;
				}

				if (item->Get<uint32>(object_fields::ItemFlags) & item_flags::Bound)
				{
					WLOG("Trade execute: item in player " << side << " trade slot " << static_cast<uint32>(i) << " is bound");
					return false;
				}

				transfers[side].push_back({ item, offer.itemSlots[i] });
			}
		}

		// Space and item limits are checked for the whole trade, not per item: several items that
		// each fit on their own could overflow the bags together, and CreateItems below runs after
		// the source items are already gone. Every offered item frees one bag slot (only carried
		// items can be offered) and is recreated as one stack, so slots balance exactly.
		for (int receiver = 0; receiver < 2; ++receiver)
		{
			const Inventory& inventory = (receiver == 0) ? inv0 : inv1;
			const auto& received = transfers[1 - receiver];
			const auto& given = transfers[receiver];

			if (received.size() > static_cast<size_t>(inventory.GetFreeSlotCount()) + given.size())
			{
				WLOG("Trade execute: player " << receiver << " does not have enough free bag slots");
				return false;
			}

			std::map<uint32, uint32> receivedCounts;
			for (const auto& transfer : received)
			{
				receivedCounts[transfer.item->GetEntry().id()] += transfer.item->GetStackCount();
			}

			for (const auto& transfer : received)
			{
				const auto& entry = transfer.item->GetEntry();
				if (entry.maxcount() > 0 && inventory.GetItemCount(entry.id()) + receivedCounts[entry.id()] > entry.maxcount())
				{
					WLOG("Trade execute: player " << receiver << " cannot carry more of item " << entry.id());
					return false;
				}
			}
		}

		// Money must not wrap: a near-capped player receiving gold used to end up nearly broke.
		const uint64 finalMoney0 = static_cast<uint64>(char0.Get<uint32>(object_fields::Money)) - m_offers[0].money + m_offers[1].money;
		const uint64 finalMoney1 = static_cast<uint64>(char1.Get<uint32>(object_fields::Money)) - m_offers[1].money + m_offers[0].money;
		if (finalMoney0 > std::numeric_limits<uint32>::max() || finalMoney1 > std::numeric_limits<uint32>::max())
		{
			WLOG("Trade execute: money would exceed the cap");
			return false;
		}

		const auto& from0to1 = transfers[0];
		const auto& from1to0 = transfers[1];

		// All validations passed — execute the transfer.
		// Remove items from source inventories
		for (const auto& transfer : from0to1)
		{
			if (inv0.RemoveItem(transfer.srcSlot) != inventory_change_failure::Okay)
			{
				ELOG("Trade execute: failed to remove item from player 0 slot " << transfer.srcSlot);
				return false;
			}
		}

		for (const auto& transfer : from1to0)
		{
			if (inv1.RemoveItem(transfer.srcSlot) != inventory_change_failure::Okay)
			{
				ELOG("Trade execute: failed to remove item from player 1 slot " << transfer.srcSlot);
				return false;
			}
		}

		// Add items to destination inventories. The checks above make a failure here a bug, but it
		// must not pass silently: the source items are already gone.
		for (const auto& transfer : from0to1)
		{
			const uint32 stackCount = transfer.item->Get<uint32>(object_fields::StackCount);
			if (inv1.CreateItems(transfer.item->GetEntry(), static_cast<uint16>(stackCount)) != inventory_change_failure::Okay)
			{
				ELOG("Trade execute: could not give " << stackCount << "x item " << transfer.item->GetEntry().id() << " to player 1 - item lost");
			}
		}

		for (const auto& transfer : from1to0)
		{
			const uint32 stackCount = transfer.item->Get<uint32>(object_fields::StackCount);
			if (inv0.CreateItems(transfer.item->GetEntry(), static_cast<uint16>(stackCount)) != inventory_change_failure::Okay)
			{
				ELOG("Trade execute: could not give " << stackCount << "x item " << transfer.item->GetEntry().id() << " to player 0 - item lost");
			}
		}

		// Transfer money
		if (m_offers[0].money > 0)
		{
			char0.ConsumeMoney(m_offers[0].money);
			char1.AddMoney(m_offers[0].money);
		}

		if (m_offers[1].money > 0)
		{
			char1.ConsumeMoney(m_offers[1].money);
			char0.AddMoney(m_offers[1].money);
		}

		// Persist both sides right away, money included: the money otherwise only reached the
		// database on the next character save, so a world crash in between kept the items moved
		// but refunded the gold. Inventory and character saves are queued on the same
		// per-character database key, so they commit in this order.
		inv0.SaveToRepository();
		inv1.SaveToRepository();
		m_players[0]->SaveCharacterData();
		m_players[1]->SaveCharacterData();

		return true;
	}

	void TradeSession::SendUpdateToPlayer(int recipientIndex, int offerIndex) const
	{
		ASSERT(recipientIndex == 0 || recipientIndex == 1);
		ASSERT(offerIndex == 0 || offerIndex == 1);

		const TradeOffer& offer = m_offers[offerIndex];
		const Inventory& inv = m_players[offerIndex]->GetCharacter().GetInventory();
		const uint8 isSelfOffer = (recipientIndex == offerIndex) ? 1 : 0;

		m_players[recipientIndex]->SendPacket([&offer, &inv, isSelfOffer](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::TradeUpdate);
			packet << io::write<uint8>(isSelfOffer);

			for (uint8 i = 0; i < TradeSlotCount; ++i)
			{
				if (offer.itemSlots[i] != 0)
				{
					const auto item = inv.GetItemAtSlot(offer.itemSlots[i]);
					if (item)
					{
						const uint32 stackCount = item->Get<uint32>(object_fields::StackCount);
						packet
							<< io::write<uint32>(item->GetEntry().id())
							<< io::write<uint32>(stackCount);
					}
					else
					{
						packet << io::write<uint32>(0) << io::write<uint32>(0);
					}
				}
				else
				{
					packet << io::write<uint32>(0) << io::write<uint32>(0);
				}
			}

			packet << io::write<uint32>(offer.money);
			packet.Finish();
		});
	}

	void TradeSession::SendAcceptUpdateToBoth() const
	{
		for (int i = 0; i < 2; ++i)
		{
			const bool myAccepted = m_offers[i].accepted;
			const bool otherAccepted = m_offers[GetOtherIndex(i)].accepted;

			m_players[i]->SendPacket([myAccepted, otherAccepted](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::TradeAcceptUpdate);
				packet << io::write<uint8>(myAccepted ? 1 : 0);
				packet << io::write<uint8>(otherAccepted ? 1 : 0);
				packet.Finish();
			});
		}
	}

	void TradeSession::NotifyOfferChanged()
	{
		InvalidateAcceptance();
		// Each player receives both their own offer and the other's offer so the client
		// always has authoritative data for both sides (isSelfOffer flag distinguishes them).
		SendUpdateToPlayer(0, 0); // player 0 gets their own offer (isSelfOffer=1)
		SendUpdateToPlayer(0, 1); // player 0 gets player 1's offer (isSelfOffer=0)
		SendUpdateToPlayer(1, 1); // player 1 gets their own offer (isSelfOffer=1)
		SendUpdateToPlayer(1, 0); // player 1 gets player 0's offer (isSelfOffer=0)
		SendAcceptUpdateToBoth();
	}
}
