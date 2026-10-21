// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_grind_state.h"

#include "base/typedefs.h"
#include "math/vector3.h"

namespace mmo
{
	class BotContext;
	struct BotEconomyState;

	/// Everything the strategies need to know about the world right now, gathered once per AI
	/// tick and then read many times.
	///
	/// The gathering is the point. Finding the nearest attackable creature is a scan over every
	/// unit the bot can see, and half a dozen triggers ask for it every tick; done naively that
	/// is the dominant cost of a swarm. Refreshing once and reading a struct afterwards turns it
	/// into one scan per bot per tick.
	///
	/// This is deliberately a fixed struct rather than a general memoized-value blackboard. Every
	/// field here is wanted by every tick of every strategy, so per-value check intervals would
	/// buy nothing and cost a map lookup per read.
	struct BotPerception final
	{
		/// False when the bot is not in the world yet, or its own object has not spawned. Every
		/// other field is meaningless in that case.
		bool valid { false };

		uint64 selfGuid { 0 };
		Vector3 position { Vector3::Zero };
		uint32 level { 1 };
		uint32 mapId { 0 };

		bool alive { false };

		/// Health and power as fractions in [0, 1], not percentages. BotUnit calls them percents
		/// but returns fractions; naming them honestly here is cheaper than being wrong by a
		/// factor of a hundred somewhere downstream.
		float healthFraction { 0.0f };
		float powerFraction { 0.0f };

		/// False for a class with no power pool at all. Without this, an empty pool and a
		/// non-existent one look identical, and a bot that can never fill the one it does not
		/// have rests forever.
		bool hasPower { false };

		/// Whether the power is mana, the only kind that resting restores.
		bool powerIsMana { false };

		bool moving { false };

		/// The unit we have selected, if it still exists.
		uint64 targetGuid { 0 };
		bool targetExists { false };
		bool targetAlive { false };

		/// Distances here ignore height, because that is the measure BotMovementController uses to
		/// decide it has arrived. Mixing the two is a live lock, not a rounding error: a target a
		/// little above or below the bot reads as out of melee range in 3D and as arrived in plan,
		/// so the bot walks to it, is instantly told it is there, decides it is not, and walks
		/// again - forever, never swinging.
		float targetDistance { 0.0f };

		/// Absolute height difference to the target. Kept apart from targetDistance for the reason
		/// above, and needed beside it: a creature in the cellar directly below the bot is at a
		/// planar distance of zero, and without this the bot fought it through the floor.
		float targetHeightDifference { 0.0f };

		/// Nearest creature we could attack, whether or not it is our current target.
		uint64 nearestAttackableGuid { 0 };
		float nearestAttackableDistance { 0.0f };

		/// How many units are currently targeting us. The bot is in combat when this is non-zero
		/// or when it is swinging at something.
		uint32 attackerCount { 0 };
		bool autoAttacking { false };

		/// Nearest dead creature with loot for the bot that it has not looted yet.
		uint64 nearestLootableGuid { 0 };
		float nearestLootableDistance { 0.0f };

		/// Free and total slots in the backpack and equipped bags.
		uint32 freeBagSlots { 0 };
		uint32 totalBagSlots { 0 };

		[[nodiscard]] bool IsInCombat() const { return attackerCount > 0 || autoAttacking; }

		/// Reads the current world state out of the session's context.
		///
		/// The grind state is needed because "nearest attackable" has to mean "nearest attackable
		/// we have not already established we cannot reach" - otherwise the bot keeps being told
		/// about the same unreachable creature and picks it again every tick.
		void Refresh(const BotContext& world, const BotGrindState& grind, const BotEconomyState& economy, GameTime nowMs);
	};
}
