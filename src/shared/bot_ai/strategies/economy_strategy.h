// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class BotAiRegistry;

	/// How close the bot walks to a corpse before asking for its loot.
	constexpr float BotLootRange = 3.0f;

	/// A loot request with no answer for this long is given up on.
	constexpr GameTime BotLootTimeoutMs = 4000;

	/// The bot goes selling once this few bag slots are left.
	constexpr uint32 BotSellAtFreeSlots = 2;

	/// How close to the vendor's spawn point the bot walks, and how far around it it looks for
	/// the vendor itself.
	constexpr float BotVendorApproachRange = 3.0f;
	constexpr float BotVendorSearchRange = 15.0f;

	/// A trip to a vendor that takes longer than this is abandoned.
	constexpr GameTime BotSellTripTimeoutMs = 5 * 60 * 1000;

	/// Registers looting, equipping upgrades and selling at vendors: the "economy_world"
	/// strategy, layered over the bot's out-of-combat strategy.
	void RegisterEconomyStrategies(BotAiRegistry& registry);
}
