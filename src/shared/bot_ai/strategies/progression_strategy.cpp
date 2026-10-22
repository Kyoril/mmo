// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "progression_strategy.h"

#include "bot_ai/bot_action.h"
#include "bot_ai/bot_ai_context.h"
#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_relevance.h"
#include "bot_ai/bot_strategy.h"
#include "bot_ai/bot_talent_builds.h"
#include "bot_ai/bot_trigger.h"
#include "bot_core/bot_context.h"
#include "bot_core/bot_nav_service.h"
#include "bot_core/bot_realm_connector.h"
#include "bot_core/bot_session.h"
#include "bot_core/bot_unit.h"

#include "log/default_log_levels.h"
#include "proto_data/project.h"

#include <memory>
#include <optional>
#include <utility>

namespace mmo
{
	namespace
	{
		/// A learned talent shows up as a spell a moment after the request; the next pick waits
		/// for it, or the bot would ask for the same rank twice.
		constexpr GameTime TalentSettleMs = 1500;

		/// The talent rank the bot's build wants next, if it has points to spend on one.
		std::optional<TalentPick> nextPick(const BotAiContext& context, BotSession& session)
		{
			const BotUnit* self = session.GetContext().GetSelf();
			const BotNavService* nav = session.GetContext().GetNavService();
			const proto::Project* project = nav ? nav->GetProject() : nullptr;
			const TalentBuildBook* book = context.GetTalentBuilds();
			if (!self || !project || !book || self->GetTalentPoints() == 0)
			{
				return std::nullopt;
			}

			const TalentBuild* build = book->Find(self->GetClassId(), context.GetRole());
			if (!build)
			{
				return std::nullopt;
			}

			const BotContext& world = session.GetContext();
			return NextTalentPick(*project, *build, [&world](const uint32 spell) { return world.HasKnownSpell(spell); });
		}

		class TalentPointsTrigger final : public BotTrigger
		{
		public:
			TalentPointsTrigger()
				: BotTrigger("talent_points_to_spend", 3000)
			{
			}

			[[nodiscard]] bool IsActive(BotAiContext& context) const override
			{
				BotSession* session = context.GetSession();
				const BotPerception& perception = context.GetPerception();
				if (!session || !perception.valid || !perception.alive || perception.IsInCombat())
				{
					return false;
				}

				if (context.GetNow() < context.GetEconomyState().lastTalentMs + TalentSettleMs)
				{
					return false;
				}

				return nextPick(context, *session).has_value();
			}
		};

		class LearnTalentAction final : public BotAction
		{
		public:
			LearnTalentAction()
				: BotAction("learn_talent")
			{
			}

			bool Execute(BotAiContext& context) override
			{
				BotSession* session = context.GetSession();
				if (!session)
				{
					return false;
				}

				const std::optional<TalentPick> pick = nextPick(context, *session);
				if (!pick)
				{
					return false;
				}

				session->GetRealm().LearnTalent(pick->talentId, pick->rank);
				context.GetEconomyState().lastTalentMs = context.GetNow();
				DLOG("Bot " << context.GetBotIndex() << " learns talent " << pick->talentId << " rank " << static_cast<uint32>(pick->rank));
				return true;
			}
		};

		class ProgressionStrategy final : public BotStrategy
		{
		public:
			ProgressionStrategy()
				: BotStrategy("progression_world")
			{
			}

			[[nodiscard]] std::vector<BotTriggerNode> GetTriggerNodes() const override
			{
				// Instant and only out of combat, so anything else may come first.
				return { { "talent_points_to_spend", { { "learn_talent", bot_relevance::High + 4 } } } };
			}
		};
	}

	void RegisterProgressionStrategies(BotAiRegistry& registry)
	{
		registry.RegisterTrigger(std::make_unique<TalentPointsTrigger>());
		registry.RegisterAction(std::make_unique<LearnTalentAction>());
		registry.RegisterStrategy(std::make_unique<ProgressionStrategy>());
	}
}
