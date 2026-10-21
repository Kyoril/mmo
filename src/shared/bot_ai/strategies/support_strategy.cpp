// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "support_strategy.h"

#include "grind_actions.h"

#include "bot_ai/bot_action.h"
#include "bot_ai/bot_ai_context.h"
#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_relevance.h"
#include "bot_ai/bot_rotation.h"
#include "bot_ai/bot_strategy.h"
#include "bot_ai/bot_trigger.h"
#include "bot_core/bot_context.h"
#include "bot_core/bot_movement_math.h"
#include "bot_core/bot_realm_connector.h"
#include "bot_core/bot_session.h"
#include "bot_core/bot_unit.h"

#include "game/spell_target_map.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace mmo
{
	namespace
	{
		/// A buff the server did not apply is not tried again for this long.
		constexpr GameTime BuffRetryMs = 30 * 1000;

		struct HealTarget
		{
			uint64 guid { 0 };
			float fraction { 1.0f };
			float distance { 0.0f };
		};

		bool isHealer(const BotAiContext& context)
		{
			return context.GetRole() == "healer";
		}

		/// Who needs healing most, if anyone is below the threshold that applies to them: the bot
		/// itself, and for healers every party member it can see.
		std::optional<HealTarget> findHealTarget(const BotAiContext& context, BotSession& session)
		{
			const BotPerception& perception = context.GetPerception();
			if (!perception.valid || !perception.alive || context.GetRotation().GetHeals().empty())
			{
				return std::nullopt;
			}

			std::optional<HealTarget> best;
			const float selfThreshold = isHealer(context) ? BotHealerSelfThreshold : BotSelfHealThreshold;
			if (perception.healthFraction < selfThreshold)
			{
				best = HealTarget { perception.selfGuid, perception.healthFraction, 0.0f };
			}

			if (!isHealer(context))
			{
				return best;
			}

			const BotContext& world = session.GetContext();
			for (const uint64 guid : session.GetRealm().GetPartyMemberGuids())
			{
				const BotUnit* member = world.GetUnit(guid);
				if (!member || guid == perception.selfGuid || !member->IsAlive() || member->GetMaxHealth() == 0)
				{
					continue;
				}

				const float fraction = static_cast<float>(member->GetHealth()) / static_cast<float>(member->GetMaxHealth());
				if (fraction >= BotHealerPartyThreshold || (best && fraction >= best->fraction))
				{
					continue;
				}

				best = HealTarget { guid, fraction, PlanarDistance(perception.position, member->GetPosition()) };
			}

			return best;
		}

		bool castOn(BotAiContext& context, BotSession& session, const BotRotationSpell& spell, const uint64 targetGuid)
		{
			SpellTargetMap targetMap;
			targetMap.SetTargetMap(spell_cast_target_flags::Unit);
			targetMap.SetUnitTarget(targetGuid);

			if (!session.GetContext().CastSpell(spell.spellId, targetMap))
			{
				return false;
			}

			context.SetNextCastTime(context.GetNow() + std::max<GameTime>(BotGlobalCooldownMs, spell.castTimeMs));
			return true;
		}

		class SupportTrigger final : public BotTrigger
		{
		public:
			using Predicate = std::function<bool(BotAiContext&, BotSession&)>;

			SupportTrigger(std::string name, Predicate predicate)
				: BotTrigger(std::move(name), 0)
				, m_predicate(std::move(predicate))
			{
			}

			[[nodiscard]] bool IsActive(BotAiContext& context) const override
			{
				BotSession* session = context.GetSession();
				return session && context.CanStartCast() && m_predicate(context, *session);
			}

		private:
			Predicate m_predicate;
		};

		class SupportAction final : public BotAction
		{
		public:
			using Body = std::function<bool(BotAiContext&, BotSession&)>;

			SupportAction(std::string name, Body body)
				: BotAction(std::move(name))
				, m_body(std::move(body))
			{
			}

			bool Execute(BotAiContext& context) override
			{
				BotSession* session = context.GetSession();
				return session && context.CanStartCast() && m_body(context, *session);
			}

		private:
			Body m_body;
		};

		class SupportStrategy final : public BotStrategy
		{
		public:
			SupportStrategy(std::string name, std::vector<BotTriggerNode> nodes)
				: BotStrategy(std::move(name))
				, m_nodes(std::move(nodes))
			{
			}

			[[nodiscard]] std::vector<BotTriggerNode> GetTriggerNodes() const override { return m_nodes; }

		private:
			std::vector<BotTriggerNode> m_nodes;
		};
	}

	void RegisterSupportStrategies(BotAiRegistry& registry)
	{
		registry.RegisterTrigger(std::make_unique<SupportTrigger>("heal_needed", [](BotAiContext& context, BotSession& session)
			{
				const std::optional<HealTarget> target = findHealTarget(context, session);
				return target && context.GetRotation().SelectHeal(session.GetContext(), target->distance) != nullptr;
			}));

		registry.RegisterTrigger(std::make_unique<SupportTrigger>("missing_self_buff", [](BotAiContext& context, BotSession& session)
			{
				const BotPerception& perception = context.GetPerception();
				if (!perception.valid || !perception.alive || perception.IsInCombat())
				{
					return false;
				}

				const BotRotationSpell* buff = context.GetRotation().SelectMissingBuff(session.GetContext());
				if (!buff)
				{
					return false;
				}

				const auto& attempts = context.GetEconomyState().lastBuffAttemptMs;
				const auto it = attempts.find(buff->spellId);
				return it == attempts.end() || context.GetNow() >= it->second + BuffRetryMs;
			}));

		registry.RegisterAction(std::make_unique<SupportAction>("cast_heal", [](BotAiContext& context, BotSession& session)
			{
				const std::optional<HealTarget> target = findHealTarget(context, session);
				if (!target)
				{
					return false;
				}

				const BotRotationSpell* heal = context.GetRotation().SelectHeal(session.GetContext(), target->distance);
				return heal && castOn(context, session, *heal, target->guid);
			}));

		registry.RegisterAction(std::make_unique<SupportAction>("cast_self_buff", [](BotAiContext& context, BotSession& session)
			{
				const BotRotationSpell* buff = context.GetRotation().SelectMissingBuff(session.GetContext());
				if (!buff)
				{
					return false;
				}

				context.GetEconomyState().lastBuffAttemptMs[buff->spellId] = context.GetNow();
				return castOn(context, session, *buff, context.GetPerception().selfGuid);
			}));

		// Out of combat: heal before resting (a heal is faster), buff before setting off.
		registry.RegisterStrategy(std::make_unique<SupportStrategy>("support_world", std::vector<BotTriggerNode> {
			{ "heal_needed", { { "cast_heal", bot_relevance::High + 7 } } },
			{ "missing_self_buff", { { "cast_self_buff", bot_relevance::High + 2 } } },
		}));

		// In combat: a heal when it is needed outranks every attack.
		registry.RegisterStrategy(std::make_unique<SupportStrategy>("support_combat", std::vector<BotTriggerNode> {
			{ "heal_needed", { { "cast_heal", bot_relevance::Interrupt + 2 } } },
		}));
	}
}
