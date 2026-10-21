// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	class BotAiRegistry;

	/// Health fractions below which a bot heals. Healers heal early and look after the party;
	/// everyone else with a heal keeps only itself alive.
	constexpr float BotHealerSelfThreshold = 0.6f;
	constexpr float BotHealerPartyThreshold = 0.75f;
	constexpr float BotSelfHealThreshold = 0.4f;

	/// Registers healing and self-buffs, derived from the spells the bot knows: "support_world"
	/// out of combat (heals and buffs) and "support_combat" in combat (heals).
	void RegisterSupportStrategies(BotAiRegistry& registry);
}
