// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "grind_triggers.h"

#include "bot_ai/bot_ai_context.h"
#include "bot_ai/bot_personality.h"
#include "bot_core/bot_session.h"
#include "bot_ai/bot_ai_registry.h"
#include "grind_actions.h"

#include <functional>
#include <memory>
#include <utility>

namespace mmo
{
	namespace
	{
		/// A trigger whose condition is a plain function of the context. Every grind trigger is
		/// one of these; giving each its own class would be a lot of ceremony for a predicate.
		class LambdaTrigger final : public BotTrigger
		{
		public:
			using Predicate = std::function<bool(BotAiContext&)>;

			LambdaTrigger(std::string name, Predicate predicate, const GameTime checkIntervalMs = 0)
				: BotTrigger(std::move(name), checkIntervalMs)
				, m_predicate(std::move(predicate))
			{
			}

			[[nodiscard]] bool IsActive(BotAiContext& context) const override
			{
				return m_predicate(context);
			}

		private:
			Predicate m_predicate;
		};

		void add(BotAiRegistry& registry, std::string name, LambdaTrigger::Predicate predicate, const GameTime intervalMs = 0)
		{
			registry.RegisterTrigger(std::make_unique<LambdaTrigger>(std::move(name), std::move(predicate), intervalMs));
		}

		[[nodiscard]] bool hasLiveTarget(const BotPerception& perception)
		{
			return perception.targetExists && perception.targetAlive;
		}
	}

	void RegisterGrindTriggers(BotAiRegistry& registry)
	{
		add(registry, "in_world", [](BotAiContext& context)
			{
				return context.GetPerception().valid;
			});

		add(registry, "dead", [](BotAiContext& context)
			{
				const BotPerception& perception = context.GetPerception();
				return perception.valid && !perception.alive;
			});

		add(registry, "no_target", [](BotAiContext& context)
			{
				const BotPerception& perception = context.GetPerception();
				return perception.valid && perception.alive && !hasLiveTarget(perception);
			});

		add(registry, "target_out_of_melee", [](BotAiContext& context)
			{
				const BotPerception& perception = context.GetPerception();
				return hasLiveTarget(perception) && perception.targetDistance > BotMeleeRange;
			});

		add(registry, "target_in_melee", [](BotAiContext& context)
			{
				const BotPerception& perception = context.GetPerception();
				return hasLiveTarget(perception) && perception.targetDistance <= BotMeleeRange;
			});

		add(registry, "attackable_nearby", [](BotAiContext& context)
			{
				const BotPerception& perception = context.GetPerception();
				if (!perception.valid || !perception.alive || perception.nearestAttackableGuid == 0)
				{
					return false;
				}

				// Only worth selecting something if we are not already fighting something. Without
				// this the bot re-selects the nearest creature every tick, which restarts the swing
				// timer each time - it swings forever and never lands a blow.
				return !hasLiveTarget(perception);
			});

		add(registry, "swing_wrong_facing", [](BotAiContext& context)
			{
				return hasLiveTarget(context.GetPerception())
					&& context.HasRecentSwingError(attack_swing_event::WrongFacing);
			});

		add(registry, "swing_out_of_range", [](BotAiContext& context)
			{
				return hasLiveTarget(context.GetPerception())
					&& context.HasRecentSwingError(attack_swing_event::OutOfRange);
			});

		add(registry, "rotation_spell_ready", [](BotAiContext& context)
			{
				const BotPerception& perception = context.GetPerception();
				if (!hasLiveTarget(perception) || !perception.alive)
				{
					return false;
				}

				if (context.GetRotation().IsEmpty() || !context.CanStartCast())
				{
					return false;
				}

				BotSession* session = context.GetSession();
				if (!session)
				{
					return false;
				}

				// Asking the rotation rather than guessing: whether anything is castable depends on
				// cooldowns, power and range together, and the rotation is where all three are known.
				return context.GetRotation().SelectSpell(session->GetContext(), perception.targetDistance) != 0;
			});

		add(registry, "needs_rest", [](BotAiContext& context)
			{
				const BotPerception& perception = context.GetPerception();
				if (!perception.valid || !perception.alive || perception.IsInCombat())
				{
					context.GetGrindState().resting = false;
					return false;
				}

				// A class with no power pool is never short of power. Treating an absent pool as an
				// empty one would leave such a bot resting for the rest of its life.
				const bool powerLow = perception.hasPower && perception.powerFraction < BotRestPowerFraction;
				const bool powerRested = !perception.hasPower || perception.powerFraction >= BotRestedPowerFraction;

				BotGrindState& grind = context.GetGrindState();

				// Per bot, so that a group does not all break off and all re-engage on the same
				// tick. A cautious bot leaves the fight early and comes back topped up; a reckless
				// one keeps swinging.
				const BotPersonality& personality = context.GetPersonality();

				if (grind.resting)
				{
					// Keep going until fully recovered, not until merely out of danger.
					grind.resting = !(perception.healthFraction >= personality.restedHealthFraction && powerRested);
					return grind.resting;
				}

				grind.resting = perception.healthFraction < personality.restHealthFraction || powerLow;
				return grind.resting;
			});

		add(registry, "no_grind_spot", [](BotAiContext& context)
			{
				return context.GetPerception().valid && !context.GetGrindState().HasSpot();
			});

		add(registry, "grind_spot_barren", [](BotAiContext& context)
			{
				const BotPerception& perception = context.GetPerception();
				const BotGrindState& grind = context.GetGrindState();

				if (!perception.valid || !perception.alive || !grind.arrived || grind.arrivedMs == 0)
				{
					return false;
				}

				if (perception.nearestAttackableGuid != 0)
				{
					return false;
				}

				// Without this the strategy has a dead end: a bot standing on a spawn point with
				// nothing alive on it matches no other trigger, and simply stops deciding.
				return context.GetNow() - grind.arrivedMs > BotGrindBarrenTimeoutMs;
			});

		add(registry, "travelling_to_grind_spot", [](BotAiContext& context)
			{
				const BotGrindState& grind = context.GetGrindState();
				return context.GetPerception().valid && grind.HasSpot() && !grind.arrived;
			});
	}
}
