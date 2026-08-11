// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_protocol/game_protocol.h"
#include "game_server/inventory_types.h"

#include <array>

using namespace mmo;

// ---------------------------------------------------------------------------
// Protocol enum sanity checks
// ---------------------------------------------------------------------------

TEST_CASE("Trade protocol opcodes are defined and ordered", "[trade][protocol]")
{
	// Client → realm opcodes exist
	REQUIRE(game::client_realm_packet::TradeInitiate < game::client_realm_packet::Count_);
	REQUIRE(game::client_realm_packet::TradeCancelRequest < game::client_realm_packet::Count_);
	REQUIRE(game::client_realm_packet::TradeAddItem < game::client_realm_packet::Count_);
	REQUIRE(game::client_realm_packet::TradeRemoveItem < game::client_realm_packet::Count_);
	REQUIRE(game::client_realm_packet::TradeSetMoney < game::client_realm_packet::Count_);
	REQUIRE(game::client_realm_packet::TradeAccept < game::client_realm_packet::Count_);
	REQUIRE(game::client_realm_packet::TradeInviteAccept < game::client_realm_packet::Count_);
	REQUIRE(game::client_realm_packet::TradeInviteDecline < game::client_realm_packet::Count_);

	// Server → client opcodes exist
	REQUIRE(game::realm_client_packet::TradeInvite < game::realm_client_packet::Count_);
	REQUIRE(game::realm_client_packet::TradeRequestResult < game::realm_client_packet::Count_);
	REQUIRE(game::realm_client_packet::TradeSessionOpened < game::realm_client_packet::Count_);
	REQUIRE(game::realm_client_packet::TradeSessionClosed < game::realm_client_packet::Count_);
	REQUIRE(game::realm_client_packet::TradeUpdate < game::realm_client_packet::Count_);
	REQUIRE(game::realm_client_packet::TradeAcceptUpdate < game::realm_client_packet::Count_);
}

TEST_CASE("Trade result enum values are distinct and reasonable", "[trade][protocol]")
{
	// Each result code should be unique and within uint8 range
	REQUIRE(game::trade_result::Success == 0);
	REQUIRE(game::trade_result::TargetBusy == 1);
	REQUIRE(game::trade_result::TooFarAway == 2);
	REQUIRE(game::trade_result::PlayerNotFound == 3);
	REQUIRE(game::trade_result::YouAreDead == 4);
	REQUIRE(game::trade_result::TargetIsDead == 5);
	REQUIRE(game::trade_result::AlreadyTrading == 6);
	REQUIRE(game::trade_result::TargetAlreadyTrading == 7);
	REQUIRE(game::trade_result::Hostile == 8);
	REQUIRE(game::trade_result::Declined == 12);
}

TEST_CASE("Trade close reason enum values are distinct", "[trade][protocol]")
{
	REQUIRE(game::trade_close_reason::Completed == 0);
	REQUIRE(game::trade_close_reason::Cancelled == 1);
	REQUIRE(game::trade_close_reason::TooFarAway == 2);
	REQUIRE(game::trade_close_reason::Death == 3);
	REQUIRE(game::trade_close_reason::Hostile == 4);
	REQUIRE(game::trade_close_reason::Disconnected == 5);
}

// ---------------------------------------------------------------------------
// Inventory slot validation for trade
// ---------------------------------------------------------------------------

TEST_CASE("Trade slot validation: equipment and bag slots are forbidden", "[trade][inventory]")
{
	SECTION("Equipment slots (0-18) are forbidden")
	{
		for (uint8 slot = 0; slot < 19; ++slot)
		{
			const auto invSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, slot);
			REQUIRE(invSlot.IsEquipment());
		}
	}

	SECTION("Bag pack slots (19-22) are forbidden")
	{
		for (uint8 slot = 19; slot <= 22; ++slot)
		{
			const auto invSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, slot);
			REQUIRE(invSlot.IsBagPack());
		}
	}

	SECTION("Backpack slots (23-38) are tradeable")
	{
		for (uint8 slot = 23; slot <= 38; ++slot)
		{
			const auto invSlot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, slot);
			REQUIRE_FALSE(invSlot.IsEquipment());
			REQUIRE_FALSE(invSlot.IsBagPack());
		}
	}
}

TEST_CASE("InventorySlot absolute encoding round-trips correctly", "[trade][inventory]")
{
	SECTION("Bag_0 slot 23 encodes and decodes")
	{
		const auto slot = InventorySlot::FromRelative(player_inventory_slots::Bag_0, 23);
		const uint16 absolute = slot.GetAbsolute();
		const auto roundTripped = InventorySlot::FromAbsolute(absolute);

		REQUIRE(roundTripped.GetAbsolute() == absolute);
		REQUIRE_FALSE(roundTripped.IsEquipment());
		REQUIRE_FALSE(roundTripped.IsBagPack());
	}

	SECTION("Zero slot value returns zero")
	{
		const auto slot = InventorySlot::FromAbsolute(0);
		REQUIRE(slot.GetAbsolute() == 0);
	}
}

// ---------------------------------------------------------------------------
// TradeOffer slot lock logic (pure data, no Player dependency)
// ---------------------------------------------------------------------------

TEST_CASE("TradeOffer slot array operations", "[trade][offer]")
{
	// Simulate the internal array state logic without a full TradeSession
	std::array<uint16, 6> slots{};

	SECTION("Slot starts empty")
	{
		for (uint8 i = 0; i < 6; ++i)
		{
			REQUIRE(slots[i] == 0);
		}
	}

	SECTION("Adding an item to a slot records the inventory slot")
	{
		const uint16 invSlot = 0xFF17;  // Bag_0 slot 23 = 0xFF00 | 23
		slots[0] = invSlot;
		REQUIRE(slots[0] == invSlot);
	}

	SECTION("Slot is considered locked when non-zero inventory slot is stored")
	{
		const uint16 invSlot = 0xFF17;
		slots[2] = invSlot;

		// Simulate IsSlotLocked logic
		bool locked = false;
		for (uint8 i = 0; i < 6; ++i)
		{
			if (slots[i] == invSlot)
			{
				locked = true;
				break;
			}
		}
		REQUIRE(locked);
	}

	SECTION("Slot is not locked after being cleared")
	{
		const uint16 invSlot = 0xFF17;
		slots[2] = invSlot;
		slots[2] = 0;  // clear

		bool locked = false;
		for (uint8 i = 0; i < 6; ++i)
		{
			if (slots[i] == invSlot)
			{
				locked = true;
				break;
			}
		}
		REQUIRE_FALSE(locked);
	}

	SECTION("Same inventory slot cannot occupy two different trade slots")
	{
		const uint16 invSlot = 0xFF17;
		slots[0] = invSlot;

		// Attempt to add same slot to slot 1 — check for duplicate
		bool duplicate = false;
		for (uint8 i = 0; i < 6; ++i)
		{
			if (i != 1 && slots[i] == invSlot)
			{
				duplicate = true;
				break;
			}
		}
		REQUIRE(duplicate);  // Should detect duplicate before allowing add
	}
}
