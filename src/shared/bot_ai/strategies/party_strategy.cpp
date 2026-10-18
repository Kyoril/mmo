// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "party_strategy.h"

#include "bot_ai/bot_action.h"
#include "bot_ai/bot_ai_context.h"
#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_relevance.h"
#include "bot_ai/bot_strategy.h"
#include "bot_ai/bot_trigger.h"
#include "bot_ai/grind_spot_index.h"
#include "bot_core/bot_context.h"
#include "bot_core/bot_movement_controller.h"
#include "bot_core/bot_movement_math.h"
#include "bot_core/bot_nav_service.h"
#include "bot_core/bot_realm_connector.h"
#include "bot_core/bot_session.h"
#include "bot_core/bot_unit.h"

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace mmo
{
	namespace
	{
		class PartyTrigger final : public BotTrigger
		{
		public:
			using Predicate = std::function<bool(BotAiContext&, BotSession&)>;

			PartyTrigger(std::string name, Predicate predicate)
				: BotTrigger(std::move(name), 0)
				, m_predicate(std::move(predicate))
			{
			}

			[[nodiscard]] bool IsActive(BotAiContext& context) const override
			{
				BotSession* session = context.GetSession();
				return session && context.GetFollowState().IsFollowing() && m_predicate(context, *session);
			}

		private:
			Predicate m_predicate;
		};

		class PartyAction final : public BotAction
		{
		public:
			using Body = std::function<bool(BotAiContext&, BotSession&)>;

			PartyAction(std::string name, Body body)
				: BotAction(std::move(name))
				, m_body(std::move(body))
			{
			}

			bool Execute(BotAiContext& context) override
			{
				BotSession* session = context.GetSession();
				return session && context.GetFollowState().IsFollowing() && m_body(context, *session);
			}

		private:
			Body m_body;
		};

		class PartyTableStrategy final : public BotStrategy
		{
		public:
			PartyTableStrategy(std::string name, std::vector<BotTriggerNode> nodes)
				: BotStrategy(std::move(name))
				, m_nodes(std::move(nodes))
			{
			}

			[[nodiscard]] std::vector<BotTriggerNode> GetTriggerNodes() const override { return m_nodes; }

		private:
			std::vector<BotTriggerNode> m_nodes;
		};

		/// Where the leader is: from their unit when the bot can see them, otherwise from the
		/// position the realm reports for party members.
		std::optional<Vector3> leaderPosition(const BotAiContext& context, BotSession& session)
		{
			const uint64 leader = context.GetFollowState().leaderGuid;
			if (const BotUnit* unit = session.GetContext().GetUnit(leader))
			{
				return unit->GetPosition();
			}

			const BotPartyMember* member = session.GetRealm().GetPartyMemberByGuid(leader);
			if (member && member->hasPosition)
			{
				return member->position;
			}

			return std::nullopt;
		}

		/// The unit the leader is fighting, if it is something the bot should fight too.
		///
		/// Only a fight the leader is actually in: a hostile they merely clicked on is left alone,
		/// or every look at a mob would become a pull.
		const BotUnit* leaderEnemy(const BotAiContext& context, BotSession& session)
		{
			const BotContext& world = session.GetContext();
			const uint64 leaderGuid = context.GetFollowState().leaderGuid;
			const BotUnit* leader = world.GetUnit(leaderGuid);
			const BotUnit* self = world.GetSelf();
			if (!leader || !self || leader->GetTargetGuid() == 0)
			{
				return nullptr;
			}

			const BotUnit* enemy = world.GetUnit(leader->GetTargetGuid());
			if (!enemy || !enemy->IsAlive() || enemy->IsPlayer() || !enemy->IsAttackableBy(*self))
			{
				return nullptr;
			}

			const proto::Project* project = world.GetNavService() ? world.GetNavService()->GetProject() : nullptr;
			if (project && IsFactionFriendly(*project, self->GetFactionTemplate(), enemy->GetFactionTemplate()))
			{
				return nullptr;
			}

			const bool engaged = enemy->GetTargetGuid() == leaderGuid
				|| enemy->GetTargetGuid() == self->GetGuid()
				|| enemy->GetHealth() < enemy->GetMaxHealth();
			return engaged ? enemy : nullptr;
		}

		float leaderDistance(const BotAiContext& context, BotSession& session)
		{
			const std::optional<Vector3> position = leaderPosition(context, session);
			return position ? PlanarDistance(session.GetContext().GetPosition(), *position) : 0.0f;
		}
	}

	void RegisterPartyStrategies(BotAiRegistry& registry)
	{
		const auto addTrigger = [&registry](std::string name, PartyTrigger::Predicate predicate)
		{
			registry.RegisterTrigger(std::make_unique<PartyTrigger>(std::move(name), std::move(predicate)));
		};

		const auto addAction = [&registry](std::string name, PartyAction::Body body)
		{
			registry.RegisterAction(std::make_unique<PartyAction>(std::move(name), std::move(body)));
		};

		addTrigger("leader_far", [](BotAiContext& context, BotSession& session)
			{
				return leaderDistance(context, session) > BotFollowDistance;
			});

		addTrigger("leader_very_far", [](BotAiContext& context, BotSession& session)
			{
				return leaderDistance(context, session) > BotFollowLeashDistance;
			});

		addTrigger("leader_enemy_to_assist", [](BotAiContext& context, BotSession& session)
			{
				const BotUnit* enemy = leaderEnemy(context, session);
				return enemy && enemy->GetGuid() != context.GetPerception().targetGuid;
			});

		addAction("follow_leader", [](BotAiContext& context, BotSession& session)
			{
				const std::optional<Vector3> position = leaderPosition(context, session);
				if (!position)
				{
					return false;
				}

				BotFollowState& follow = context.GetFollowState();
				BotMovementController& mover = session.GetMovementController();

				// Chasing a moving target: keep the current path while it still leads to roughly
				// where they are, and re-path now and then rather than on every decision.
				const bool destinationStale = PlanarDistance(follow.destination, *position) > BotFollowRepathDistance;
				const bool mayRepath = context.GetNow() >= follow.pathMs + BotFollowRepathIntervalMs;
				if (mover.IsActive() && !(destinationStale && mayRepath))
				{
					return true;
				}

				if (!mover.MoveTo(session.GetContext(), *position, BotFollowAcceptance))
				{
					return false;
				}

				follow.destination = *position;
				follow.pathMs = context.GetNow();
				return true;
			});

		addAction("assist_leader", [](BotAiContext& context, BotSession& session)
			{
				const BotUnit* enemy = leaderEnemy(context, session);
				if (!enemy)
				{
					return false;
				}

				session.GetContext().GetRealmConnector()->SetSelection(enemy->GetGuid());
				return true;
			});

		// Out of combat, in place of grind_world: no grind spots, no wandering. Catching up
		// outranks resting, so the bot does not sit down while its leader walks off; joining the
		// leader's fight outranks both, and getting into range for it outranks everything.
		registry.RegisterStrategy(std::make_unique<PartyTableStrategy>("party_world", std::vector<BotTriggerNode> {
			{ "leader_far", { { "follow_leader", bot_relevance::High + 2 } } },
			{ "needs_rest", { { "rest", bot_relevance::High + 1 } } },
			{ "leader_enemy_to_assist", { { "assist_leader", bot_relevance::High + 3 } } },
			{ "target_out_of_melee", { { "approach_target", bot_relevance::Move } } },
			{ "target_in_melee", { { "auto_attack", bot_relevance::Normal } } },
			{ "rotation_spell_ready", { { "cast_rotation_spell", bot_relevance::Normal + 2 } } },
		}));

		// In combat, over grind_combat: fight what the leader fights rather than the nearest
		// thing, and break off to catch up when they have run on.
		registry.RegisterStrategy(std::make_unique<PartyTableStrategy>("party_combat", std::vector<BotTriggerNode> {
			{ "leader_enemy_to_assist", { { "assist_leader", bot_relevance::High + 1 } } },
			{ "leader_very_far", { { "follow_leader", bot_relevance::Interrupt + 1 } } },
		}));
	}
}
