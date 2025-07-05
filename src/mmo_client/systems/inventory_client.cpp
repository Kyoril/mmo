// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "inventory_client.h"

#include "frame_ui/frame_mgr.h"
#include "game/item.h"
#include "log/default_log_levels.h"

namespace mmo
{
	InventoryClient::InventoryClient(RealmConnector& realmConnector)
		: m_realmConnector(realmConnector)
	{
	}

	void InventoryClient::Initialize()
	{
		// Register packet handlers
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::InventoryError, *this, &InventoryClient::OnInventoryError);
	}

	void InventoryClient::Shutdown()
	{
		m_packetHandlerConnections.Clear();
	}

	PacketParseResult InventoryClient::OnInventoryError(game::IncomingPacket& packet)
	{
		uint8 errorCode;
		if (!(packet >> io::read<uint8>(errorCode)))
		{
			return PacketParseResult::Disconnect;
		}

		// Map inventory error codes to string keys for localization
		static const char* s_errorStrings[] = {
			"EQUIP_ERR_OK",                             // 0 - Okay
			"EQUIP_ERR_CANT_EQUIP_LEVEL_I",             // 1 - CantEquipLevel
			"EQUIP_ERR_CANT_EQUIP_SKILL",               // 2 - CantEquipSkill
			"EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT",         // 3 - ItemDoesNotGoToSlot
			"EQUIP_ERR_BAG_FULL",                       // 4 - BagFull
			"EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG",    // 5 - NonEmptyBagOverOtherBag
			"EQUIP_ERR_CANT_TRADE_EQUIP_BAGS",          // 6 - CantTradeEquipBags
			"EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE",          // 7 - OnlyAmmoCanGoHere
			"EQUIP_ERR_NO_REQUIRED_PROFICIENCY",        // 8 - NoRequiredProficiency
			"EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE",    // 9 - NoEquipmentSlotAvailable
			"EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM",    // 10 - YouCanNeverUseThatItem
			"EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM2",   // 11 - YouCanNeverUseThatItem2
			"EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE2",   // 12 - NoEquipmentSlotAvailable2
			"EQUIP_ERR_CANT_EQUIP_WITH_TWOHANDED",      // 13 - CantEquipWithTwoHanded
			"EQUIP_ERR_CANT_DUAL_WIELD",                // 14 - CantDualWield
			"EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG",        // 15 - ItemDoesntGoIntoBag
			"EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG2",       // 16 - ItemDoesntGoIntoBag2
			"EQUIP_ERR_CANT_CARRY_MORE_OF_THIS",        // 17 - CantCarryMoreOfThis
			"EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE3",   // 18 - NoEquipmentSlotAvailable3
			"EQUIP_ERR_ITEM_CANT_STACK",                // 19 - ItemCantStack
			"EQUIP_ERR_ITEM_CANT_BE_EQUIPPED",          // 20 - ItemCantBeEquipped
			"EQUIP_ERR_ITEMS_CANT_BE_SWAPPED",          // 21 - ItemsCantBeSwapped
			"EQUIP_ERR_SLOT_IS_EMPTY",                  // 22 - SlotIsEmpty
			"EQUIP_ERR_ITEM_NOT_FOUND",                 // 23 - ItemNotFound
			"EQUIP_ERR_CANT_DROP_SOULBOUND",            // 24 - CantDropSoulbound
			"EQUIP_ERR_OUT_OF_RANGE",                   // 25 - OutOfRange
			"EQUIP_ERR_TRIED_TO_SPLIT_MORE_THAN_COUNT", // 26 - TriedToSplitMoreThanCount
			"EQUIP_ERR_COULDNT_SPLIT_ITEMS",            // 27 - CouldntSplitItems
			"EQUIP_ERR_MISSING_REAGENT",                // 28 - MissingReagent
			"EQUIP_ERR_NOT_ENOUGH_MONEY",               // 29 - NotEnoughMoney
			"EQUIP_ERR_NOT_A_BAG",                      // 30 - NotABag
			"EQUIP_ERR_CAN_ONLY_DO_WITH_EMPTY_BAGS",    // 31 - CanOnlyDoWithEmptyBags
			"EQUIP_ERR_DONT_OWN_THAT_ITEM",             // 32 - DontOwnThatItem
			"EQUIP_ERR_CAN_EQUIP_ONLY1_QUIVER",         // 33 - CanEquipOnlyOneQuiver
			"EQUIP_ERR_MUST_PURCHASE_THAT_BAG_SLOT",    // 34 - MustPurchaseThatBagSlot
			"EQUIP_ERR_TOO_FAR_AWAY_FROM_BANK",         // 35 - TooFarAwayFromBank
			"EQUIP_ERR_ITEM_LOCKED",                    // 36 - ItemLocked
			"EQUIP_ERR_YOU_ARE_STUNNED",                // 37 - YouAreStunned
			"EQUIP_ERR_YOU_ARE_DEAD",                   // 38 - YouAreDead
			"EQUIP_ERR_CANT_DO_RIGHT_NOW",              // 39 - CantDoRightNow
			"EQUIP_ERR_INTERNAL_BAG_ERROR",             // 40 - InternalBagError
			"EQUIP_ERR_CAN_EQUIP_ONLY1_QUIVER2",        // 41 - CanEquipOnlyOneQuiver2
			"EQUIP_ERR_CAN_EQUIP_ONLY1_AMMOPOUCH",      // 42 - CanEquipOnlyOneAmmoPouch
			"EQUIP_ERR_STACKABLE_CANT_BE_WRAPPED",      // 43 - StackableCantBeWrapped
			"EQUIP_ERR_EQUIPPED_CANT_BE_WRAPPED",       // 44 - EquippedCantBeWrapped
			"EQUIP_ERR_WRAPPED_CANT_BE_WRAPPED",        // 45 - WrappedCantBeWrapped
			"EQUIP_ERR_BOUND_CANT_BE_WRAPPED",          // 46 - BoundCantBeWrapped
			"EQUIP_ERR_UNIQUE_CANT_BE_WRAPPED",         // 47 - UniqueCantBeWrapped
			"EQUIP_ERR_BAGS_CANT_BE_WRAPPED",           // 48 - BagsCantBeWrapped
			"EQUIP_ERR_ALREADY_LOOTED",                 // 49 - AlreadyLooted
			"EQUIP_ERR_INVENTORY_FULL",                 // 50 - InventoryFull
			"EQUIP_ERR_BANK_FULL",                      // 51 - BankFull
			"EQUIP_ERR_ITEM_IS_CURRENTLY_SOLD_OUT",     // 52 - ItemIsCurrentlySoldOut
			"EQUIP_ERR_BAG_FULL3",                      // 53 - BagFull3
			"EQUIP_ERR_ITEM_NOT_FOUND2",                // 54 - ItemNotFound2
			"EQUIP_ERR_ITEM_CANT_STACK2",               // 55 - ItemCantStack2
			"EQUIP_ERR_BAG_FULL4",                      // 56 - BagFull4
			"EQUIP_ERR_ITEM_SOLD_OUT",                  // 57 - ItemSoldOut
			"EQUIP_ERR_OBJECT_IS_BUSY",                 // 58 - ObjectIsBusy
			"EQUIP_ERR_NONE",                           // 59 - None
			"EQUIP_ERR_NOT_IN_COMBAT",                  // 60 - NotInCombat
			"EQUIP_ERR_NOT_WHILE_DISARMED",             // 61 - NotWhileDisarmed
			"EQUIP_ERR_BAG_FULL6",                      // 62 - BagFull6
			"EQUIP_ERR_CANT_EQUIP_RANK",                // 63 - CantEquipBank
			"EQUIP_ERR_CANT_EQUIP_REPUTATION",          // 64 - CantEquipReputation
			"EQUIP_ERR_TOO_MANY_SPECIAL_BAGS",          // 65 - TooManySpecialBags
			"EQUIP_ERR_LOOT_CANT_LOOT_THAT_NOW",        // 66 - CantLootThatNow
			"EQUIP_ERR_ITEM_UNIQUE_EQUIPPABLE",         // 67 - ItemUniqueEquippable
			"EQUIP_ERR_VENDOR_MISSING_TURNINS",         // 68 - VendorMissingTurnIns
			"EQUIP_ERR_NOT_ENOUGH_HONOR_POINTS",        // 69 - NotEnoughHonorPoints
			"EQUIP_ERR_NOT_ENOUGH_ARENA_POINTS",        // 70 - NotEnoughArenaPoints
			"EQUIP_ERR_ITEM_MAX_COUNT_SOCKETED",        // 71 - ItemMaxCountSocketed
			"EQUIP_ERR_MAIL_BOUND_ITEM",                // 72 - MailBoundItem
			"EQUIP_ERR_NO_SPLIT_WHILE_PROSPECTING",     // 73 - NoSplitWhileProspecting
			"EQUIP_ERR_ITEM_MAX_COUNT_EQUIPPED_SOCKETED",// 75 - ItemMaxCountEquippedSocketed
			"EQUIP_ERR_ITEM_UNIQUE_EQUIPPABLE_SOCKETED", // 76 - ItemUniqueEquippableSocketed
			"EQUIP_ERR_TOO_MUCH_GOLD",                  // 77 - TooMuchGold
			"EQUIP_ERR_NOT_DURING_ARENA_MATCH",         // 78 - NotDuringArenaMatch
			"EQUIP_ERR_CANNOT_TRADE_THAT",              // 79 - CannotTradeThat
			"EQUIP_ERR_PERSONAL_ARENA_RATING_TOO_LOW"   // 80 - PersonalArenaRatingTooLow
		};

		// Make sure the error code is valid
		if (errorCode < sizeof(s_errorStrings) / sizeof(s_errorStrings[0]))
		{
			// Skip "Okay" errors as they aren't actually errors
			if (errorCode != inventory_change_failure::Okay)
			{
				// Trigger the UI error message event
				FrameManager::Get().TriggerLuaEvent("UI_ERROR_MESSAGE", s_errorStrings[errorCode]);
			}
		}
		else
		{
			WLOG("Received invalid inventory error code: " << static_cast<uint32>(errorCode));
		}

		return PacketParseResult::Pass;
	}
}
