// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player.h"

#include "player_manager.h"
#include "base/utilities.h"
#include "game_server/game_creature_s.h"
#include "game_server/game_object_s.h"
#include "game_server/game_player_s.h"
#include "game_server/game_bag_s.h"
#include "proto_data/project.h"
#include "game/loot.h"
#include "game/vendor.h"

namespace mmo
{

	namespace
	{
		void WriteQuestMenuEntry(const proto::QuestEntry& quest, QuestgiverStatus status, io::Writer& writer)
		{
			writer
				<< io::write<uint32>(quest.id())
				<< io::write<uint32>(status)
				<< io::write<int32>(quest.questlevel())
				<< io::write_dynamic_range<uint8>(quest.name());
		}
	}

	void Player::OnQuestGiverHello(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 questGiverGuid;
		if (!(contentReader >> io::read<uint64>(questGiverGuid)))
		{
			ELOG("Failed to read QuestGiverHello packet!");
			return;
		}

		const GameCreatureS* unit = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(questGiverGuid);
		if (!unit)
		{
			return;
		}

		DLOG("Quest Giver Hello on npc " << log_hex_digit(questGiverGuid) << " (" << unit->GetEntry().name() << ")");

		// Is this unit a quest giver?
		if (unit->GetEntry().quests_size() == 0 && unit->GetEntry().end_quests_size() == 0)
		{
			WLOG("Unit " << log_hex_digit(questGiverGuid) << " has no quests to offer or turn in!");
			return;
		}

		// Send the quest list to the client
		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		typename game::Protocol::OutgoingPacket packet(sink);
		packet.Start(game::realm_client_packet::QuestGiverQuestList);
		packet << io::write<uint64>(questGiverGuid) << io::write_dynamic_range<uint16>(unit->GetEntry().greeting_text());	// TODO: 512 cap

		const size_t questCountPos = sink.Position();

		uint8 questCount = 0;
		packet << io::write<uint8>(0); // Placeholder for real quest count

		for (const auto& questId : unit->GetEntry().end_quests())
		{
			auto questStatus = m_character->GetQuestStatus(questId);
			if (questStatus == quest_status::Incomplete || questStatus == quest_status::Complete)
			{
				if (const auto* quest = m_project.quests.getById(questId))
				{
					WriteQuestMenuEntry(*quest, questStatus == quest_status::Incomplete ? questgiver_status::Incomplete : questgiver_status::RewardRep, packet);
					questCount++;
				}
			}
		}
		for (const auto& questId : unit->GetEntry().quests())
		{
			auto questStatus = m_character->GetQuestStatus(questId);
			if (questStatus == quest_status::Available)
			{
				if (const auto* quest = m_project.quests.getById(questId))
				{
					WriteQuestMenuEntry(*quest, questgiver_status::Chat, packet);
					questCount++;
				}
			}
		}

		// Overwrite real quest count in menu before finish
		sink.Overwrite(questCountPos, reinterpret_cast<const char*>(&questCount), sizeof(questCount));
		packet.Finish();

		// Send proxy packet
		m_connector.SendProxyPacket(m_character->GetGuid(), packet.GetId(), packet.GetSize(), buffer);
	}

	void Player::OnQuestGiverStatusQuery(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 questGiverGuid;
		if (!(contentReader >> io::read<uint64>(questGiverGuid)))
		{
			ELOG("Failed to read QuestGiverStatusQuery packet!");
			return;
		}

		// Find quest giver
		GameCreatureS* questGiver = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(questGiverGuid);
		if (!questGiver)
		{
			ELOG("Unable to find quest giver!");
			return;
		}

		DLOG("Requested quest giver status query for npc " << log_hex_digit(questGiverGuid) << " (" << questGiver->GetEntry().name() << ")");

		QuestgiverStatus status = questGiver->GetQuestGiverStatus(*m_character);
		SendPacket([questGiverGuid, status](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::QuestGiverStatus);
				packet << io::write<uint64>(questGiverGuid) << io::write<uint8>(status);
				packet.Finish();
			});
	}

	void Player::OnTrainerBuySpell(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 trainerGuid;
		uint32 spellId;
		if (!(contentReader >> io::read<uint64>(trainerGuid) >> io::read<uint32>(spellId)))
		{
			ELOG("Failed to read TrainerBuySpell packet!");
			return;
		}

		DLOG("Player wants to buy trainer spell " << spellId << " from trainer " << log_hex_digit(trainerGuid));

		ASSERT(m_character);
		ASSERT(m_character->GetWorldInstance());

		if (m_character->HasSpell(spellId))
		{
			ELOG("Player already knows that spell!");
			return;
		}

		GameCreatureS* trainerNpc = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(trainerGuid);
		if (!trainerNpc)
		{
			ELOG("Unable to find trainer npc!");
			return;
		}

		const auto* trainerEntry = m_project.trainers.getById(trainerNpc->GetEntry().trainerentry());
		if (!trainerEntry)
		{
			ELOG("Trainer npc does not seem to be an actual trainer!");
			return;
		}

		// Check basic interaction
		if (!trainerNpc->IsInteractable(*m_character))
		{
			return;
		}

		for (const auto& trainerSpellEntry : trainerEntry->spells())
		{
			if (trainerSpellEntry.spell() == spellId)
			{
				const proto::SpellEntry* spellEntry = m_project.spells.getById(spellId);
				if (!spellEntry)
				{
					ELOG("Unknown spell " << spellId << " offered by trainer id " << log_hex_digit(trainerEntry->id()) << ", can't buy spell!");
					return;
				}

				if (m_character->GetLevel() < trainerSpellEntry.reqlevel())
				{
					SendTrainerBuyError(trainerGuid, trainer_result::FailedLevelTooLow);
					return;
				}

				if (!m_character->ConsumeMoney(trainerSpellEntry.spellcost()))
				{
					SendTrainerBuyError(trainerGuid, trainer_result::FailedNotEnoughMoney);
					return;
				}

				m_character->AddSpell(spellId);
				SendTrainerBuySucceeded(trainerGuid, spellId);
				return;
			}
		}
	}

	void Player::OnTrainerMenu(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 trainerGuid;
		if (!(contentReader >> io::read<uint64>(trainerGuid)))
		{
			ELOG("Failed to read TrainerMenu packet!");
			return;
		}

		const GameCreatureS* unit = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(trainerGuid);
		if (!unit)
		{
			return;
		}

		DLOG("Requested trainer menu from npc " << log_hex_digit(trainerGuid) << " (" << unit->GetEntry().name() << ")");

		// Is this unit a trainer?
		const proto::UnitEntry& unitEntry = unit->GetEntry();
		const proto::TrainerEntry* trainer = nullptr;
		if (unitEntry.trainerentry() != 0)
		{
			trainer = m_project.trainers.getById(unitEntry.trainerentry());
		}

		if (!trainer)
		{
			return;
		}

		HandleTrainerGossip(*trainer, *unit);
	}

	void Player::OnListInventory(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 vendorGuid;
		if (!(contentReader >> io::read<uint64>(vendorGuid)))
		{
			ELOG("Failed to read ListInventory packet!");
			return;
		}

		const GameCreatureS* unit = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(vendorGuid);
		if (!unit)
		{
			return;
		}

		DLOG("Requested vendor inventory from npc " << log_hex_digit(vendorGuid) << " (" << unit->GetEntry().name() << ")");

		// Is this unit a trainer?
		const proto::UnitEntry& unitEntry = unit->GetEntry();
		const proto::VendorEntry* vendor = nullptr;
		if (unitEntry.vendorentry() != 0)
		{
			vendor = m_project.vendors.getById(unitEntry.vendorentry());
		}

		if (!vendor)
		{
			return;
		}

		HandleVendorGossip(*vendor, *unit);
	}

	void Player::OnSellItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 vendorGuid, itemGuid;
		if (!(contentReader >> io::read<uint64>(vendorGuid) >> io::read<uint64>(itemGuid)))
		{
			WLOG("Failed to read vendor guid and item guid");
			return;
		}

		// Find vendor
		GameCreatureS* vendor = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(vendorGuid);
		if (!vendor)
		{
			ELOG("Can't find vendor!");
			return;
		}

		// Check basic interaction
		if (!vendor->IsInteractable(*m_character))
		{
			return;
		}

		uint16 itemSlot = 0;
		if (!m_character->GetInventory().FindItemByGUID(itemGuid, itemSlot))
		{
			ELOG("Can't find item!");
			return;
		}

		// Find the item by it's guid
		auto item = m_character->GetInventory().GetItemAtSlot(itemSlot);
		if (!item)
		{
			ELOG("Can't find item at slot!");
			return;
		}

		uint32 stack = item->GetStackCount();
		uint32 money = stack * item->GetEntry().sellprice();
		if (money == 0)
		{
			ELOG("Can't sell item!");
			return;
		}

		// TODO: Overflow protection!
		m_character->GetInventory().RemoveItem(itemSlot, stack, true);
		m_character->Set<uint32>(object_fields::Money, m_character->Get<uint32>(object_fields::Money) + money);
	}

	void Player::OnBuyItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 vendorGuid = 0;
		uint32 item = 0;
		uint8 count = 0;
		if (!(contentReader >> io::read<uint64>(vendorGuid) >> io::read<uint32>(item) >> io::read<uint8>(count)))
		{
			WLOG("Failed to read BuyItem packet!");
			return;
		}

		if (count == 0) count = 1;

		if (!m_character->IsAlive())
		{
			ELOG("Can't buy items while character is dead!");
			return;
		}

		// Find item entry
		const proto::ItemEntry* itemEntry = m_project.items.getById(item);
		if (itemEntry == nullptr)
		{
			ELOG("Player wants to buy unknown item!");
			return;
		}

		uint32 buyCount = itemEntry->buycount();
		if (buyCount == 0) buyCount = 1;

		uint8 totalCount = count * buyCount;

		ASSERT(m_character->GetWorldInstance());
		const GameCreatureS* vendor = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(vendorGuid);
		if (!vendor)
		{
			ELOG("Unable to find vendor npc!");
			return;
		}

		// Check basic interaction
		if (!vendor->IsInteractable(*m_character))
		{
			return;
		}

		const auto* vendorEntry = m_project.vendors.getById(vendor->GetEntry().vendorentry());
		if (vendorEntry == nullptr || vendorEntry->items_size() <= 0)
		{
			ELOG("Npc has no vendor entry assigned and thus does not sell anything!");
			return;
		}

		if (auto vendorItemEntryIt = std::find_if(vendorEntry->items().begin(), vendorEntry->items().end(), [item](const proto::VendorItemEntry& vendorItem)
			{
				return vendorItem.item() == item;
			}); vendorItemEntryIt == vendorEntry->items().end())
		{
			ELOG("Vendor does not sell item!");
			return;
		}

		// Take money
		uint32 price = itemEntry->buyprice() * count;
		uint32 money = m_character->Get<uint32>(object_fields::Money);
		if (money < price)
		{
			ELOG("Not enough money to buy item from vendor");
			return;
		}

		std::map<uint16, uint16> addedBySlot;
		if (auto result = m_character->GetInventory().CreateItems(*itemEntry, totalCount, &addedBySlot); result != inventory_change_failure::Okay)
		{
			ELOG("Failed to create items in inventory!");
			//m_character->inventoryChangeFailure(result, nullptr, nullptr);
			return;
		}

		m_character->Set<uint32>(object_fields::Money, money - price);

		// Send push notifications
		for (auto& slot : addedBySlot)
		{
			auto item = m_character->GetInventory().GetItemAtSlot(slot.first);
			if (item)
			{
				// TODO: Send notification
			}
		}
	}

	void Player::OnGossipHello(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 objectGuid;
		if (!(contentReader >> io::read<uint64>(objectGuid)))
		{
			WLOG("Failed to read object guid");
			return;
		}

		const GameCreatureS* unit = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(objectGuid);
		if (!unit)
		{
			return;
		}

		DLOG("Requested gossip menu from npc " << log_hex_digit(objectGuid) << " (" << unit->GetEntry().name() << ")");

		// Is this unit a trainer?
		const proto::UnitEntry& unitEntry = unit->GetEntry();
		const proto::TrainerEntry* trainer = nullptr;
		const proto::VendorEntry* vendor = nullptr;
		if (unitEntry.trainerentry() != 0)
		{
			trainer = m_project.trainers.getById(unitEntry.trainerentry());
		}

		if (unitEntry.vendorentry() != 0)
		{
			vendor = m_project.vendors.getById(unitEntry.vendorentry());
		}

		if (vendor && !trainer)
		{
			HandleVendorGossip(*vendor, *unit);
		}
		else if (trainer && !vendor)
		{
			HandleTrainerGossip(*trainer, *unit);
		}
		else
		{
			// TODO: Handle other gossip
		}
	}

}