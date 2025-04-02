// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player.h"

#include "player_manager.h"
#include "base/utilities.h"
#include "game/gossip.h"
#include "game_server/game_creature_s.h"
#include "game_server/game_object_s.h"
#include "game_server/game_player_s.h"
#include "game_server/game_bag_s.h"
#include "proto_data/project.h"
#include "game/loot.h"
#include "game/vendor.h"
#include "game_server/condition_mgr.h"

namespace mmo
{
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

		if (!unit->IsInteractable(*m_character))
		{
			return;
		}

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
		SerializeQuestList(*unit, packet);
		packet.Finish();

		// Send proxy packet
		m_connector.SendProxyPacket(m_character->GetGuid(), packet.GetId(), packet.GetSize(), buffer);
	}

	void Player::OnAcceptQuest(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 questGiverGuid = 0;
		uint32 questId = 0;
		if (!(contentReader >> io::read<uint64>(questGiverGuid) >> io::read<uint32>(questId)))
		{
			ELOG("Failed to read AcceptQuest packet!");
			return;
		}

		// Check if the quest exists
		const proto::QuestEntry* quest = m_project.quests.getById(questId);
		if (!quest)
		{
			WLOG("Tried to accept unknown quest id");
			return;
		}

		// Check if that object exists and provides the requested quest
		ASSERT(m_worldInstance);
		GameObjectS* questGiver = m_worldInstance->FindByGuid<GameObjectS>(questGiverGuid);
		if (!questGiver || !questGiver->ProvidesQuest(questId))
		{
			return;
		}

		// We need this check since the quest can fail for various other reasons
		if (m_character->IsQuestlogFull())
		{
			SendPacket([this](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::QuestLogFull);
				packet.Finish();
			});
			return;
		}

		// Accept that quest
		if (!m_character->AcceptQuest(questId))
		{
			ELOG("Failed to accept quest " << questId);
			return;
		}

		DLOG("Player " << m_characterData.name << " accepted quest " << questId << " from quest giver object " << log_hex_digit(questGiverGuid));

		// Ensure that the gossip menu is closed
		CloseGossip();
	}

	void Player::OnAbandonQuest(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 questId = 0;
		if (!(contentReader >> io::read<uint32>(questId)))
		{
			ELOG("Failed to read AbandonQuest packet!");
			return;
		}

		if (!m_character->AbandonQuest(questId))
		{
			ELOG("Failed to abandon quest " << questId);
		}
	}

	void Player::OnQuestGiverQueryQuest(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 questGiverGuid = 0;
		uint32 questId = 0;
		if (!(contentReader >> io::read<uint64>(questGiverGuid) >> io::read<uint32>(questId)))
		{
			ELOG("Failed to read AcceptQuest packet!");
			return;
		}

		const auto* quest = m_project.quests.getById(questId);
		if (!quest)
		{
			return;
		}

		ASSERT(m_worldInstance);

		GameObjectS* questGiverObject = m_worldInstance->FindByGuid<GameObjectS>(questGiverGuid);
		if (!questGiverObject)
		{
			return;
		}

		if (!questGiverObject->ProvidesQuest(questId))
		{
			return;
		}

		if (!questGiverObject->IsInteractable(*m_character))
		{
			return;
		}

		SendQuestDetails(questGiverGuid, *quest);
	}

	void Player::QuestGiverChooseQuestReward(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 questGiverGuid = 0;
		uint32 questId = 0, rewardChoice = 0;
		if (!(contentReader >> io::read<uint64>(questGiverGuid) >> io::read<uint32>(questId) >> io::read<uint32>(rewardChoice)))
		{
			ELOG("Failed to read QuestGiverChooseQuestReward packet!");
			return;
		}

		// Find quest giver
		GameCreatureS* questGiver = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(questGiverGuid);
		if (!questGiver)
		{
			ELOG("Unable to find quest giver!");
			return;
		}

		const auto* quest = m_project.quests.getById(questId);
		if (!quest)
		{
			return;
		}

		ASSERT(m_worldInstance);

		GameObjectS* questGiverObject = m_worldInstance->FindByGuid<GameObjectS>(questGiverGuid);
		if (!questGiverObject)
		{
			return;
		}

		if (!questGiverObject->EndsQuest(questId))
		{
			return;
		}

		if (!questGiverObject->IsInteractable(*m_character))
		{
			return;
		}

		// Reward this quest
		if (bool result = m_character->RewardQuest(questGiverGuid, questId, rewardChoice))
		{
			// If the quest should perform a spell cast on the players character, we will do so now
			if (quest->rewardspellcast())
			{
				if (const auto* rewardSpell = m_project.spells.getById(quest->rewardspellcast()))
				{
					// Prepare the spell cast target map
					SpellTargetMap targetMap;
					targetMap.SetTargetMap(spell_cast_target_flags::Self);
					targetMap.SetUnitTarget(m_character->GetGuid());
					m_character->CastSpell(targetMap, *rewardSpell, 0, true);
				}
			}

			// Try to find next quest and if there is one, send quest details
			if (uint32 nextQuestId = quest->nextchainquestid(); nextQuestId &&
				questGiverObject->ProvidesQuest(nextQuestId) &&
				m_character->GetQuestStatus(nextQuestId) == quest_status::Available)
			{
				if (const auto* nextQuestEntry = m_project.quests.getById(nextQuestId))
				{
					SendQuestDetails(questGiverGuid, *nextQuestEntry);
				}
			}
		}
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

		QuestgiverStatus status = questGiver->GetQuestGiverStatus(*m_character);
		SendPacket([questGiverGuid, status](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::QuestGiverStatus);
				packet << io::write<uint64>(questGiverGuid) << io::write<uint8>(status);
				packet.Finish();
			});
	}

	void Player::OnQuestGiverCompleteQuest(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 questGiverGuid = 0;
		uint32 questId = 0;
		if (!(contentReader >> io::read<uint64>(questGiverGuid) >> io::read<uint32>(questId)))
		{
			ELOG("Failed to read AcceptQuest packet!");
			return;
		}

		const auto* quest = m_project.quests.getById(questId);
		if (!quest)
		{
			ELOG("Tried to complete unknown quest " << questId);
			return;
		}

		ASSERT(m_worldInstance);

		GameObjectS* questGiverObject = m_worldInstance->FindByGuid<GameObjectS>(questGiverGuid);
		if (!questGiverObject)
		{
			ELOG("Unable to find questgiver object " << log_hex_digit(questGiverGuid) << " while trying to complete quest " << questId);
			return;
		}

		if (!questGiverObject->EndsQuest(questId))
		{
			ELOG("Quest " << questId << " is not ended by questgiver " << log_hex_digit(questGiverGuid));
			return;
		}

		if (!questGiverObject->IsInteractable(*m_character))
		{
			return;
		}

		// Check the quest status
		const QuestStatus questStatus = m_character->GetQuestStatus(questId);
		if (questStatus != quest_status::Complete)
		{
			SendPacket([questGiverGuid, quest, this, questStatus](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::QuestGiverRequestItems);
					packet
						<< io::write<uint64>(questGiverGuid)
						<< io::write<uint32>(quest->id())
						<< io::write_dynamic_range<uint8>(quest->name())
						<< io::write_dynamic_range<uint16>(quest->requestitemstext());

					const size_t itemCountPos = packet.Sink().Position();
					uint16 itemCount = 0;
					packet << io::write<uint16>(itemCount);

					for (const auto& request : quest->requirements())
					{
						if (request.itemid() == 0)
						{
							continue;
						}

						itemCount++;
						packet
							<< io::write<uint32>(request.itemid())
							<< io::write<uint32>(request.itemcount());
						const auto* item = m_project.items.getById(request.itemid());
						packet
							<< io::write<uint32>(item ? item->displayid() : 0);
					}

					packet.Sink().Overwrite(itemCountPos, reinterpret_cast<const char*>(&itemCount), sizeof(itemCount));
					packet.Finish();
				});
			return;
		}

		SendQuestReward(questGiverGuid, *quest);
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
		uint32 price = itemEntry->buyprice() * totalCount;
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

		if (!unit->IsInteractable(*m_character))
		{
			return;
		}

		// Lets search for npc's gossip menu entries and check conditions
		const proto::GossipMenuEntry* gossipMenuToSend = nullptr;
		for (const auto gossip : unit->GetEntry().gossip_menus())
		{
			const auto* gossipMenu = m_project.gossipMenus.getById(gossip);
			if (!gossipMenu)
			{
				ELOG("Gossip menu " << gossip << " not found but assigned to npc " << unit->GetEntry().id() << " (" << unit->GetEntry().name() << ")");
				continue;
			}

			// Has this gossip menu a condition?
			if (gossipMenu->conditionid() == 0)
			{
				// No condition means we are always going to use this menu!
				gossipMenuToSend = gossipMenu;
				break;
			}
			else
			{
				if (m_conditionMgr.PlayerMeetsCondition(*m_character, gossipMenu->conditionid()))
				{
					gossipMenuToSend = gossipMenu;
					break;
				}
			}
		}

		// Has a gossip menu to send?
		if (gossipMenuToSend != nullptr)
		{
			DLOG("Sending gossip menu " << gossipMenuToSend->name() << " to player");
			SendGossipMenu(*unit, *gossipMenuToSend);
		}
		else
		{
			// TODO: Validate if this is the right thing for us to do here

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
		}
	}

	void Player::HandleGossipAction(GameCreatureS& unit, uint32 menuId, const proto::GossipMenuOption& action)
	{
		switch (action.action_type())
		{
		case gossip_actions::None:
			CloseGossip();
			break;

		case gossip_actions::GossipMenu:
			{
				const auto* menu = m_project.gossipMenus.getById(action.action_param());
				if (!menu)
				{
					ELOG("Gossip menu " << action.action_param() << " not found but assigned to npc " << unit.GetEntry().id() << " (" << unit.GetEntry().name() << ")");
					return;
				}

				SendGossipMenu(unit, *menu);
			}
			break;

		case gossip_actions::Trainer:
			{
				const auto* trainer = m_project.trainers.getById(unit.GetEntry().trainerentry());
				if (!trainer)
				{
					ELOG("Trainer " << unit.GetEntry().trainerentry() << " not found but assigned to npc " << unit.GetEntry().id() << " (" << unit.GetEntry().name() << ")");
					return;
				}
				SendTrainerList(*trainer, unit);
			}
			break;

		case gossip_actions::Vendor:
			{
				const auto* vendor = m_project.vendors.getById(unit.GetEntry().vendorentry());
				if (!vendor)
				{
					ELOG("Vendor " << unit.GetEntry().vendorentry() << " not found but assigned to npc " << unit.GetEntry().id() << " (" << unit.GetEntry().name() << ")");
					return;
				}
				SendVendorInventory(*vendor, unit);
			}
			break;

		case gossip_actions::Trigger:
			unit.RaiseTrigger(trigger_event::OnGossipAction, { menuId, action.id() }, m_character.get());
			CloseGossip();
			break;
		}
	}

	void Player::CloseGossip()
	{
		SendPacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::GossipComplete);
				packet.Finish();
			});
	}

	void Player::OnGossipAction(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 objectGuid;
		uint32 menuId;
		uint32 actionId;
		if (!(contentReader >> io::read<uint64>(objectGuid) >> io::read<uint32>(menuId) >> io::read<uint32>(actionId)))
		{
			WLOG("Failed to read object guid");
			return;
		}

		GameCreatureS* unit = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(objectGuid);
		if (!unit)
		{
			return;
		}

		if (!unit->IsInteractable(*m_character))
		{
			return;
		}

		// Check if this npc has the gossip menu and if the gossip menu and the gossip actions are available and usable for the player
		const proto::GossipMenuEntry* gossipMenu = GetActiveGossipMenuFromNpc(*unit);
		if (!gossipMenu)
		{
			ELOG("Player tried to interact with npc which does not offer a gossip menu at all for the player");
			return;
		}

		if (gossipMenu->id() != menuId)
		{
			ELOG("Player tried to interact with npc with wrong gossip menu id");
			return;
		}

		// Now find the gossip action
		for (const mmo::proto::GossipMenuOption& action : gossipMenu->options())
		{
			if (action.id() == actionId)
			{
				if (action.conditionid() != 0)
				{
					if (!m_conditionMgr.PlayerMeetsCondition(*m_character, action.conditionid()))
					{
						ELOG("Player tried to interact with npc with gossip action " << actionId << "  in menu " << menuId << " but condition is not met");
						return;
					}
				}

				// Handle the gossip action
				HandleGossipAction(*unit, gossipMenu->id(), action);
				return;
			}
		}

		ELOG("Player tried to execute gossip action " << actionId << " but menu " << menuId << " does not offer this action for the player");
	}

	const proto::GossipMenuEntry* Player::GetActiveGossipMenuFromNpc(const GameCreatureS& npc) const
	{
		// Lets search for npc's gossip menu entries and check conditions
		for (const auto gossip : npc.GetEntry().gossip_menus())
		{
			const auto* gossipMenu = m_project.gossipMenus.getById(gossip);
			if (!gossipMenu)
			{
				ELOG("Gossip menu " << gossip << " not found but assigned to npc " << npc.GetEntry().id() << " (" << npc.GetEntry().name() << ")");
				continue;
			}

			// Has this gossip menu a condition?
			if (gossipMenu->conditionid() == 0)
			{
				// No condition means we are always going to use this menu!
				return gossipMenu;
			}

			if (m_conditionMgr.PlayerMeetsCondition(*m_character, gossipMenu->conditionid()))
			{
				return gossipMenu;
			}
		}

		return nullptr;
	}
}
