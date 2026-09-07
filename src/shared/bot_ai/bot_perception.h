// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

namespace mmo
{
	class BotContext;

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
		float healthPercent { 0.0f };
		float powerPercent { 0.0f };
		bool moving { false };

		/// The unit we have selected, if it still exists.
		uint64 targetGuid { 0 };
		bool targetExists { false };
		bool targetAlive { false };
		float targetDistance { 0.0f };

		/// Nearest creature we could attack, whether or not it is our current target.
		uint64 nearestAttackableGuid { 0 };
		float nearestAttackableDistance { 0.0f };

		/// How many units are currently targeting us. The bot is in combat when this is non-zero
		/// or when it is swinging at something.
		uint32 attackerCount { 0 };
		bool autoAttacking { false };

		[[nodiscard]] bool IsInCombat() const { return attackerCount > 0 || autoAttacking; }

		/// Reads the current world state out of the session's context.
		void Refresh(const BotContext& world);
	};
}
