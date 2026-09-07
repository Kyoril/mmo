// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "grind_actions.h"
#include "grind_triggers.h"

#include "bot_ai/bot_ai_context.h"
#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_relevance.h"
#include "bot_ai/grind_spot_index.h"
#include "bot_core/bot_context.h"
#include "bot_core/bot_movement_controller.h"
#include "bot_core/bot_movement_math.h"
#include "bot_core/bot_realm_connector.h"
#include "bot_core/bot_session.h"
#include "bot_core/bot_unit.h"

#include "log/default_log_levels.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

namespace mmo
{
	namespace
	{
		/// Re-pathing every tick would flood the server with movement packets and make the bot
		/// stutter. A chasing bot only re-paths once its quarry has moved further than this from
		/// where the current path is headed.
		constexpr float RepathThreshold = 2.5f;

		/// An action defined by a lambda over the session. Every grind action needs the session,
		/// so the lookup and the null check live here rather than in each of them.
		class SessionAction final : public BotAction
		{
		public:
			using Body = std::function<bool(BotAiContext&, BotSession&)>;

			SessionAction(std::string name, Body body)
				: BotAction(std::move(name))
				, m_body(std::move(body))
			{
			}

			BotNextActionList prerequisites;
			BotNextActionList alternatives;

			[[nodiscard]] bool IsUseful(BotAiContext& context) const override
			{
				return context.GetSession() != nullptr;
			}

			bool Execute(BotAiContext& context) override
			{
				BotSession* session = context.GetSession();
				if (!session)
				{
					return false;
				}

				return m_body(context, *session);
			}

			[[nodiscard]] BotNextActionList GetPrerequisites() const override { return prerequisites; }
			[[nodiscard]] BotNextActionList GetAlternatives() const override { return alternatives; }

		private:
			Body m_body;
		};

		SessionAction& add(BotAiRegistry& registry, std::string name, SessionAction::Body body)
		{
			auto action = std::make_unique<SessionAction>(std::move(name), std::move(body));
			SessionAction& ref = *action;
			registry.RegisterAction(std::move(action));
			return ref;
		}

		/// Gives up on the current spot and remembers not to try it again for a while.
		void abandonSpot(BotAiContext& context, const std::string& reason)
		{
			BotGrindState& grind = context.GetGrindState();
			if (grind.HasSpot())
			{
				grind.Blacklist(grind.spotIndex, context.GetNow(), BotGrindBlacklistMs);
				DLOG("Bot " << context.GetBotIndex() << " abandoning grind spot " << grind.spotIndex << ": " << reason);
			}

			grind.ClearSpot();
		}
	}

	void RegisterGrindActions(BotAiRegistry& registry)
	{
		// ------------------------------------------------------------------
		// Finding somewhere to fight
		// ------------------------------------------------------------------

		add(registry, "pick_grind_spot", [](BotAiContext& context, BotSession&)
			{
				const GrindSpotIndex* index = context.GetGrindSpots();
				if (!index)
				{
					return false;
				}

				const BotPerception& perception = context.GetPerception();
				const uint32 minLevel = perception.level > BotGrindLevelsBelow
					? perception.level - BotGrindLevelsBelow
					: 1;

				const std::vector<std::size_t> candidates = index->FindCandidates(
					perception.mapId,
					perception.position,
					minLevel,
					perception.level + BotGrindLevelsAbove,
					BotGrindSearchRadius,
					BotGrindCandidateChoices * 4);

				std::vector<std::size_t> usable;
				usable.reserve(BotGrindCandidateChoices);
				for (const std::size_t candidate : candidates)
				{
					if (context.GetGrindState().IsBlacklisted(candidate, context.GetNow()))
					{
						continue;
					}

					usable.push_back(candidate);
					if (usable.size() >= BotGrindCandidateChoices)
					{
						break;
					}
				}

				if (usable.empty())
				{
					return false;
				}

				// Random among the nearest few rather than strictly nearest, so that bots in the
				// same region spread out instead of queueing at one spawn.
				const std::size_t chosen = usable[context.RollRange(0, static_cast<uint32>(usable.size() - 1))];

				BotGrindState& grind = context.GetGrindState();
				grind.spotIndex = chosen;
				grind.spotPosition = index->GetSpot(chosen).position;
				grind.arrived = false;
				return true;
			});

		add(registry, "travel_to_grind_spot", [](BotAiContext& context, BotSession& session)
			{
				BotGrindState& grind = context.GetGrindState();
				if (!grind.HasSpot())
				{
					return false;
				}

				BotMovementController& mover = session.GetMovementController();
				BotContext& world = session.GetContext();

				if (mover.GetStatus() == BotMovementStatus::Unreachable
					|| mover.GetStatus() == BotMovementStatus::Stuck)
				{
					abandonSpot(context, mover.GetLastReason());
					return false;
				}

				// Planar, matching what BotMovementController uses to decide it has arrived. A 3D
				// distance here would be a live lock rather than a rounding error: a spawn point a
				// little above or below the bot reads as far away in 3D and as arrived in plan, so
				// the bot re-paths, is instantly told it has arrived, decides it has not, and
				// re-paths again - forever, without ever moving.
				if (PlanarDistance(world.GetPosition(), grind.spotPosition) <= BotGrindSpotArrivalRange)
				{
					if (!grind.arrived)
					{
						grind.arrived = true;
						grind.arrivedMs = context.GetNow();
					}

					return true;
				}

				if (mover.IsActive())
				{
					// Already on the way. Consuming the tick is exactly right: a travelling bot
					// has nothing better to do.
					return true;
				}

				if (!mover.MoveTo(world, grind.spotPosition, BotGrindSpotArrivalRange))
				{
					abandonSpot(context, "no_path");
					return false;
				}

				return true;
			});

		add(registry, "abandon_grind_spot", [](BotAiContext& context, BotSession&)
			{
				BotGrindState& grind = context.GetGrindState();
				if (!grind.HasSpot())
				{
					return false;
				}

				// Briefly, not for the full unreachable period: whatever lives here is almost
				// certainly just respawning, and this spot is worth another look soon.
				grind.Blacklist(grind.spotIndex, context.GetNow(), BotGrindBarrenBlacklistMs);
				grind.ClearSpot();
				return true;
			});

		// ------------------------------------------------------------------
		// Fighting
		// ------------------------------------------------------------------

		add(registry, "select_target", [](BotAiContext& context, BotSession& session)
			{
				const uint64 guid = context.GetPerception().nearestAttackableGuid;
				if (guid == 0)
				{
					return false;
				}

				session.GetContext().GetRealmConnector()->SetSelection(guid);
				return true;
			});

		add(registry, "approach_target", [](BotAiContext& context, BotSession& session)
			{
				const BotPerception& perception = context.GetPerception();
				BotContext& world = session.GetContext();

				const BotUnit* target = world.GetUnit(perception.targetGuid);
				if (!target)
				{
					return false;
				}

				BotMovementController& mover = session.GetMovementController();
				const Vector3 targetPosition = target->GetPosition();

				if (mover.IsActive())
				{
					// Chasing something that has barely moved does not justify a new path.
					if ((mover.GetTarget() - targetPosition).GetLength() < RepathThreshold)
					{
						return true;
					}
				}

				return mover.MoveTo(world, targetPosition, BotMeleeRange);
			});

		add(registry, "face_target", [](BotAiContext& context, BotSession& session)
			{
				const uint64 guid = context.GetPerception().targetGuid;
				if (guid == 0)
				{
					return false;
				}

				session.GetContext().FaceUnit(guid);
				return true;
			});

		add(registry, "auto_attack", [](BotAiContext& context, BotSession& session)
			{
				const BotPerception& perception = context.GetPerception();
				if (perception.targetGuid == 0)
				{
					return false;
				}

				BotContext& world = session.GetContext();

				// Already swinging at the right thing. Re-sending AttackStart would restart the
				// swing timer, which is the difference between a bot that kills things and one
				// that stands next to them forever.
				if (world.IsAutoAttacking() && world.GetAutoAttackTarget() == perception.targetGuid)
				{
					return true;
				}

				world.StartAutoAttack(perception.targetGuid);
				return true;
			}).prerequisites = { { "face_target", bot_relevance::Normal } };

		// ------------------------------------------------------------------
		// Staying alive
		// ------------------------------------------------------------------

		add(registry, "rest", [](BotAiContext&, BotSession& session)
			{
				BotContext& world = session.GetContext();

				if (world.IsAutoAttacking())
				{
					world.StopAutoAttack();
				}

				if (session.GetMovementController().IsActive())
				{
					session.GetMovementController().Stop(world, "resting");
				}

				// Regeneration is the server's job. The bot only has to stop doing the things
				// that would interrupt it, and let the tick pass.
				return true;
			});

		add(registry, "revive", [](BotAiContext& context, BotSession& session)
			{
				session.GetContext().GetRealmConnector()->SendReviveRequest();

				// Whatever the bot was doing before it died is not worth resuming: it lost the
				// fight there.
				abandonSpot(context, "died");
				return true;
			});
	}
}
