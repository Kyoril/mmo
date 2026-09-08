// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	class BotAiRegistry;

	/// Registers the triggers, actions and strategies a solo grinding bot needs, and the names
	/// the three engines are built from.
	///
	/// The strategies are:
	///   grind_world  - non-combat: find somewhere to fight, walk there, pull, rest.
	///   grind_combat - combat: close the distance, face, swing, break off when losing.
	///   grind_dead   - dead: release and revive.
	void RegisterGrindStrategies(BotAiRegistry& registry);
}
