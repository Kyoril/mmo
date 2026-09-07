// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "grind_strategy.h"
#include "grind_actions.h"
#include "grind_triggers.h"

#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_relevance.h"

#include <memory>
#include <utility>

namespace mmo
{
	namespace
	{
		/// A strategy built from literal trigger nodes. Behaviour here is data, not code - which
		/// is what will let the same tables come from Lua later without the engine noticing.
		class TableStrategy final : public BotStrategy
		{
		public:
			TableStrategy(std::string name, std::vector<BotTriggerNode> nodes, BotNextActionList defaults = {})
				: BotStrategy(std::move(name))
				, m_nodes(std::move(nodes))
				, m_defaults(std::move(defaults))
			{
			}

			[[nodiscard]] std::vector<BotTriggerNode> GetTriggerNodes() const override { return m_nodes; }
			[[nodiscard]] BotNextActionList GetDefaultActions() const override { return m_defaults; }

		private:
			std::vector<BotTriggerNode> m_nodes;
			BotNextActionList m_defaults;
		};

		void addStrategy(BotAiRegistry& registry, std::string name, std::vector<BotTriggerNode> nodes, BotNextActionList defaults = {})
		{
			registry.RegisterStrategy(std::make_unique<TableStrategy>(std::move(name), std::move(nodes), std::move(defaults)));
		}
	}

	void RegisterGrindStrategies(BotAiRegistry& registry)
	{
		RegisterGrindTriggers(registry);
		RegisterGrindActions(registry);

		// Out of combat. The ordering that matters: resting outranks pulling, so a bot that just
		// won a fight heals up before starting the next one instead of chaining fights until it
		// dies. Picking a spot outranks travelling to one only because a bot without a spot has
		// nowhere to travel to.
		addStrategy(registry, "grind_world",
			{
				{ "no_grind_spot", { { "pick_grind_spot", bot_relevance::High } } },
				{ "needs_rest", { { "rest", bot_relevance::High + 1 } } },
				{ "travelling_to_grind_spot", { { "travel_to_grind_spot", bot_relevance::Move } } },
				{ "grind_spot_barren", { { "abandon_grind_spot", bot_relevance::High } } },
				{ "attackable_nearby", { { "select_target", bot_relevance::Normal } } },
				{ "target_out_of_melee", { { "approach_target", bot_relevance::Move } } },
				{ "target_in_melee", { { "auto_attack", bot_relevance::Normal } } },
			});

		// In combat. Approaching outranks swinging because a swing out of range is a wasted
		// packet and a swing error, and breaking off outranks both.
		addStrategy(registry, "grind_combat",
			{
				{ "low_health", { { "rest", bot_relevance::Emergency } } },
				{ "no_target", { { "select_target", bot_relevance::High } } },
				{ "target_out_of_melee", { { "approach_target", bot_relevance::Move } } },
				{ "target_in_melee", { { "auto_attack", bot_relevance::Normal } } },
			});

		// Dead. Nothing else applies, which is why this is its own engine rather than a
		// multiplier vetoing most of the world strategy.
		addStrategy(registry, "grind_dead",
			{
				{ "dead", { { "revive", bot_relevance::Emergency } } },
			});
	}
}
