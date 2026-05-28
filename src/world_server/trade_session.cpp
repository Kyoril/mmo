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

		m_offers[playerIndex].itemSlots[tradeSlot] = inventorySlot;
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

		std::vector<ItemTransfer> from0to1;
		std::vector<ItemTransfer> from1to0;

		for (uint8 i = 0; i < TradeSlotCount; ++i)
		{
			if (m_offers[0].itemSlots[i] != 0)
			{
				auto item = inv0.GetItemAtSlot(m_offers[0].itemSlots[i]);
				if (!item)
				{
					WLOG("Trade execute: item in player 0 trade slot " << static_cast<uint32>(i) << " no longer exists");
					return false;
				}
				from0to1.push_back({ item, m_offers[0].itemSlots[i] });
			}

			if (m_offers[1].itemSlots[i] != 0)
			{
				auto item = inv1.GetItemAtSlot(m_offers[1].itemSlots[i]);
				if (!item)
				{
					WLOG("Trade execute: item in player 1 trade slot " << static_cast<uint32>(i) << " no longer exists");
					return false;
				}
				from1to0.push_back({ item, m_offers[1].itemSlots[i] });
			}
		}

		// Verify receiving players have enough inventory space
		for (const auto& transfer : from0to1)
		{
			const uint32 stackCount = transfer.item->Get<uint32>(object_fields::StackCount);
			if (inv1.CanStoreItems(transfer.item->GetEntry(), static_cast<uint16>(stackCount)) != inventory_change_failure::Okay)
			{
				WLOG("Trade execute: player 1 does not have inventory space for item " << transfer.item->GetEntry().id());
				return false;
			}
		}

		for (const auto& transfer : from1to0)
		{
			const uint32 stackCount = transfer.item->Get<uint32>(object_fields::StackCount);
			if (inv0.CanStoreItems(transfer.item->GetEntry(), static_cast<uint16>(stackCount)) != inventory_change_failure::Okay)
			{
				WLOG("Trade execute: player 0 does not have inventory space for item " << transfer.item->GetEntry().id());
				return false;
			}
		}

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

		// Add items to destination inventories
		for (const auto& transfer : from0to1)
		{
			const uint32 stackCount = transfer.item->Get<uint32>(object_fields::StackCount);
			inv1.CreateItems(transfer.item->GetEntry(), static_cast<uint16>(stackCount));
		}

		for (const auto& transfer : from1to0)
		{
			const uint32 stackCount = transfer.item->Get<uint32>(object_fields::StackCount);
			inv0.CreateItems(transfer.item->GetEntry(), static_cast<uint16>(stackCount));
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

		// Save both inventories
		inv0.SaveToRepository();
		inv1.SaveToRepository();

		return true;
	}

	void TradeSession::SendUpdateToPlayer(int recipientIndex) const
	{
		ASSERT(recipientIndex == 0 || recipientIndex == 1);

		const int offerIndex = GetOtherIndex(recipientIndex);
		const TradeOffer& offer = m_offers[offerIndex];
		const Inventory& inv = m_players[offerIndex]->GetCharacter().GetInventory();

		m_players[recipientIndex]->SendPacket([&offer, &inv](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::TradeUpdate);

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
		SendUpdateToPlayer(0);
		SendUpdateToPlayer(1);
		SendAcceptUpdateToBoth();
	}
}
