// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "grind_actions.h"

#include "bot_ai/grind_spot_claims.h"
#include "grind_triggers.h"

#include "bot_ai/bot_ai_context.h"
#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_personality.h"
#include "bot_ai/bot_relevance.h"
#include "bot_ai/bot_rotation.h"
#include "bot_ai/grind_spot_index.h"
#include "bot_core/bot_context.h"
#include "bot_core/bot_movement_controller.h"
#include "bot_core/bot_movement_math.h"
#include "bot_core/bot_realm_connector.h"
#include "bot_core/bot_session.h"
#include "bot_core/bot_unit.h"

#include "game/spell.h"
#include "game/spell_target_map.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cmath>
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

				const BotPersonality& personality = context.GetPersonality();

				std::vector<std::size_t> usable;
				usable.reserve(personality.candidateChoices);

				// The nearest spots first, then further afield if every one of them is blacklisted.
				// A cluster of unreachable spawns - rats in a cellar the mesh does not connect -
				// otherwise blacklists everything within reach and the bot wanders on the spot for
				// the five minutes until the first entry expires.
				for (const uint32 widening : { 1u, 4u, 16u })
				{
					const std::vector<std::size_t> candidates = index->FindCandidates(
						perception.mapId,
						perception.position,
						minLevel,
						perception.level + BotGrindLevelsAbove,
						BotGrindSearchRadius * personality.searchRadiusScale * (widening == 16u ? 2.0f : 1.0f),
						personality.candidateChoices * 4 * widening);

					for (const std::size_t candidate : candidates)
					{
						if (context.GetGrindState().IsBlacklisted(candidate, context.GetNow()))
						{
							continue;
						}

						usable.push_back(candidate);
						if (usable.size() >= personality.candidateChoices * 3)
						{
							break;
						}
					}

					if (!usable.empty())
					{
						break;
					}
				}

				if (usable.empty())
				{
					return false;
				}

				// Weighed rather than nearest: a spot other bots already work counts as further away,
				// and so does one well below the bot's level, so that the swarm spreads over the
				// world and moves on to new areas as it levels instead of crowding the nearest spawn.
				// Random among the best few, so that bots in one region still do not all agree.
				const GrindSpotClaims* claims = context.GetGrindClaims();
				std::vector<std::pair<float, std::size_t>> scored;
				scored.reserve(usable.size());
				for (const std::size_t candidate : usable)
				{
					const GrindSpot& spot = index->GetSpot(candidate);
					const float distance = (spot.position - perception.position).GetLength();
					const float crowding = claims ? static_cast<float>(claims->GetCount(candidate)) : 0.0f;
					const float levelsBelow = spot.maxLevel < perception.level
						? static_cast<float>(perception.level - spot.maxLevel)
						: 0.0f;
					scored.emplace_back(distance + crowding * BotGrindCrowdingCost + levelsBelow * BotGrindLevelGapCost, candidate);
				}

				std::sort(scored.begin(), scored.end());
				const std::size_t best = std::min<std::size_t>(personality.candidateChoices, scored.size());
				const std::size_t chosen = scored[context.RollRange(0, static_cast<uint32>(best - 1))].second;

				BotGrindState& grind = context.GetGrindState();
				grind.spotIndex = chosen;
				if (GrindSpotClaims* sharedClaims = context.GetMutableGrindClaims())
				{
					sharedClaims->Claim(context.GetBotIndex(), chosen);
				}

				// Offset from the spawn point, not onto it. Every bot working a spot would otherwise
				// walk to the identical coordinate and stand inside the others already there.
				grind.spotPosition = OffsetAround(index->GetSpot(chosen).position,
					personality.approachAngle, personality.spotOffset);
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
				const Vector3 selfPosition = world.GetPosition();
				if (PlanarDistance(selfPosition, grind.spotPosition) <= BotGrindSpotArrivalRange
					&& std::abs(selfPosition.y - grind.spotPosition.y) <= BotGrindSpotArrivalHeight)
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
					// The bot's own offset from the spawn may be somewhere it cannot stand. Try the
					// spawn point itself before writing the spot off entirely.
					const GrindSpotIndex* index = context.GetGrindSpots();
					const bool reachedExactSpot = index
						&& mover.MoveTo(world, index->GetSpot(grind.spotIndex).position, BotGrindSpotArrivalRange);

					if (!reachedExactSpot)
					{
						abandonSpot(context, "no_path");
						return false;
					}
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
				BotGrindState& grind = context.GetGrindState();

				const BotUnit* target = world.GetUnit(perception.targetGuid);
				if (!target)
				{
					return false;
				}

				// Getting into melee range is the only thing that proves the approach worked, so it is
				// the only thing that clears the failure count. Clearing it whenever a path was
				// successfully planned would reset the counter every tick and the bot would never give
				// up on anything.
				if (perception.targetDistance <= BotMeleeRange)
				{
					if (perception.targetHeightDifference <= BotMeleeHeightTolerance)
					{
						grind.ClearApproachFailures();
						return true;
					}

					// Right above or below it - a floor in between. Walking there again would arrive
					// at once and change nothing, so this is a failed approach like any other, and
					// enough of them write the target off.
					if (grind.NoteApproachFailure(perception.targetGuid) >= BotApproachFailureLimit)
					{
						grind.MarkUnreachable(perception.targetGuid, context.GetNow(), BotUnreachableUnitMs);
						grind.ClearApproachFailures();
						world.GetRealmConnector()->SetSelection(0);
						world.StopAutoAttack();
					}

					return false;
				}

				BotMovementController& mover = session.GetMovementController();

				// Two ways an approach fails, and only one of them is MoveTo returning false. The
				// other is a path that plans fine and then runs out before arriving - a target on a
				// ledge, or across a gap in the mesh. Both have to count, or the bot spends its whole
				// life re-planning a route that never gets there.
				const bool movementFailed = mover.GetStatus() == BotMovementStatus::Unreachable
					|| mover.GetStatus() == BotMovementStatus::Stuck;

				if (movementFailed
					&& grind.NoteApproachFailure(perception.targetGuid) >= BotApproachFailureLimit)
				{
					// A creature that cannot be reached is still the nearest thing the bot could
					// attack, so nothing about the world will make it stop choosing this one.
					grind.MarkUnreachable(perception.targetGuid, context.GetNow(), BotUnreachableUnitMs);
					grind.ClearApproachFailures();

					// Dropping the selection is what lets the target triggers fire again.
					world.GetRealmConnector()->SetSelection(0);
					world.StopAutoAttack();
					return false;
				}

				const Vector3 targetPosition = target->GetPosition();

				// Each bot stands at its own angle around the creature instead of walking onto its
				// exact position. Without this a group attacking one thing converges on a single
				// coordinate and ends up occupying the same space as a stack of characters.
				const BotPersonality& personality = context.GetPersonality();
				const Vector3 standPosition = OffsetAround(targetPosition,
					personality.approachAngle, personality.combatOffset);

				if (mover.IsActive())
				{
					// Chasing something that has barely moved does not justify a new path.
					if ((mover.GetTarget() - standPosition).GetLength() < RepathThreshold)
					{
						return true;
					}
				}

				if (mover.MoveTo(world, standPosition, BotCombatStandAcceptance))
				{
					return true;
				}

				// The chosen spot may be inside a wall or off the navigation mesh. Spreading out is a
				// preference, not a reason to give up on a fight, so fall back to the creature itself
				// before counting this as a failed approach.
				if (mover.MoveTo(world, targetPosition, BotMeleeRange))
				{
					return true;
				}

				if (grind.NoteApproachFailure(perception.targetGuid) >= BotApproachFailureLimit)
				{
					grind.MarkUnreachable(perception.targetGuid, context.GetNow(), BotUnreachableUnitMs);
					grind.ClearApproachFailures();
					world.GetRealmConnector()->SetSelection(0);
					world.StopAutoAttack();
				}

				return false;
			});

		add(registry, "cast_rotation_spell", [](BotAiContext& context, BotSession& session)
			{
				const BotPerception& perception = context.GetPerception();
				if (perception.targetGuid == 0 || !context.CanStartCast())
				{
					return false;
				}

				BotContext& world = session.GetContext();
				const BotRotationSpell* spell = context.GetRotation().SelectSpellEntry(world, perception.targetDistance);
				if (!spell)
				{
					return false;
				}

				// Damage spells are rejected server-side if the caster is not facing the target, so
				// turning is part of casting rather than a separate action that might not run first.
				world.FaceUnit(perception.targetGuid);

				SpellTargetMap targetMap;
				targetMap.SetTargetMap(spell_cast_target_flags::Unit);
				targetMap.SetUnitTarget(perception.targetGuid);

				if (!world.CastSpell(spell->spellId, targetMap))
				{
					return false;
				}

				// A cast that is under way blocks the next one for the longer of the global cooldown
				// and its own cast time. Sending the packet only queues it, and the send says nothing
				// about whether the server will accept it, so this is the only brake there is.
				context.SetNextCastTime(context.GetNow() + std::max<GameTime>(BotGlobalCooldownMs, spell->castTimeMs));
				return true;
			}).alternatives = { { "auto_attack", bot_relevance::Normal } };

		add(registry, "face_target", [](BotAiContext& context, BotSession& session)
			{
				const uint64 guid = context.GetPerception().targetGuid;
				if (guid == 0)
				{
					return false;
				}

				session.GetContext().FaceUnit(guid);

				// The correction has been sent; keeping the error would make this fire on every tick
				// of its window and starve the actual fight. If the facing is still wrong the server
				// says so again on the next swing.
				context.ClearSwingError();
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
			});
		// Deliberately no face_target prerequisite. A prerequisite only runs when IsPossible says
		// no, and the bot cannot tell whether the server considers it to be facing its target -
		// that is precisely what the swing_wrong_facing trigger is for. A prerequisite here would
		// look like it handled facing while never once running.

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

		add(registry, "wander", [](BotAiContext& context, BotSession& session)
			{
				const BotPerception& perception = context.GetPerception();
				if (!perception.valid || !perception.alive)
				{
					return false;
				}

				BotMovementController& mover = session.GetMovementController();
				if (mover.IsActive())
				{
					// Already drifting somewhere. Consuming the tick keeps the bot from re-rolling a
					// new destination every fifth of a second and never arriving at any of them.
					return true;
				}

				BotContext& world = session.GetContext();
				const BotGrindState& grind = context.GetGrindState();

				// Drift around the spot the bot came here for, not around wherever it happens to be
				// standing. Wandering off is what a bot with no destination does; a bot working a spawn
				// that is briefly empty wants to still be in range of it when something comes back, and
				// drifting away cost about fifteen percent of the kill rate when this centred on self.
				const Vector3 center = grind.HasSpot() ? grind.spotPosition : world.GetPosition();

				const float angle = static_cast<float>(context.RollRange(0, 359)) * 0.0174533f;
				const float radius = BotWanderMinRadius
					+ static_cast<float>(context.RollRange(0, static_cast<uint32>(BotWanderMaxRadius - BotWanderMinRadius)));

				// A destination off the navigation mesh simply fails and a different one is rolled on
				// the next tick, which is the whole error handling this needs.
				return mover.MoveTo(world, OffsetAround(center, angle, radius), BotWanderAcceptance);
			});

		add(registry, "revive", [](BotAiContext& context, BotSession& session)
			{
				BotGrindState& grind = context.GetGrindState();

				// Ask once and wait. Repeating the request every tick makes the server run its whole
				// revive path again each time - including a teleport to the bind point, which is a
				// hand-off to another world node when the bind map is not the one the bot died on.
				if (grind.reviveRequestedMs != 0
					&& context.GetNow() - grind.reviveRequestedMs < BotReviveRetryMs)
				{
					return true;
				}

				grind.reviveRequestedMs = context.GetNow();
				session.GetContext().GetRealmConnector()->SendReviveRequest();

				// Whatever the bot was doing before it died is not worth resuming: it lost the
				// fight there.
				abandonSpot(context, "died");
				return true;
			});
	}
}
