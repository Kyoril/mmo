// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/movement_info.h"
#include "math/radian.h"
#include "math/vector3.h"

#include <cstddef>
#include <vector>

namespace mmo
{
	[[nodiscard]] bool IsFiniteVector(const Vector3& value);
	[[nodiscard]] Vector3 FlattenToGround(const Vector3& value);
	[[nodiscard]] float PlanarDistanceSquared(const Vector3& a, const Vector3& b);
	[[nodiscard]] float PlanarDistance(const Vector3& a, const Vector3& b);
	[[nodiscard]] Vector3 SafeNormalizePlanar(const Vector3& value, const Vector3& fallback = Vector3::UnitZ);
	// Facing normalization lives in math/math_utils.h as NormalizeFacingSigned, so the bot and
	// the engine cannot disagree about what a facing means.
	[[nodiscard]] Radian ComputeFacingTo(const Vector3& from, const Vector3& to, const Radian& fallback = Radian(0.0f));
	[[nodiscard]] float SmallestAngleDelta(const Radian& a, const Radian& b);
	[[nodiscard]] bool IsDegenerateSegment(const Vector3& start, const Vector3& end, float epsilon = 1e-3f);
	[[nodiscard]] Vector3 TrimSegmentEnd(const Vector3& start, const Vector3& end, float trimDistance);
	[[nodiscard]] Vector3 ComputeFollowStandOffTarget(
		const Vector3& selfPosition,
		const Vector3& anchorPosition,
		const Radian& anchorFacing,
		bool anchorHasFacing,
		float desiredDistance);
	[[nodiscard]] Vector3 ComputeSmoothedPathTarget(
		const std::vector<Vector3>& points,
		std::size_t currentIndex,
		float turnThresholdRadians,
		float maxTrimDistance);

	/// @brief Which waypoint the bot is heading for, and the exact point it steers at.
	struct BotPathFollowState final
	{
		/// @brief Index of the waypoint being headed for; equals the point count once the path is used up.
		std::size_t waypointIndex { 0 };
		/// @brief The point to steer at: the waypoint, pulled back along the incoming segment on turns.
		Vector3 steeringTarget { Vector3::Zero };
		/// @brief True once every waypoint has been passed.
		bool exhausted { false };
	};

	/// @brief Picks the waypoint to head for from where the bot currently stands, skipping the ones
	///        it is already standing on.
	///
	/// Acceptance is measured against the smoothed steering target, not against the raw waypoint,
	/// because the steering target is the point the bot actually steers at and stops on. Measuring
	/// the two with different points leaves a band as wide as the corner trim in which the bot has
	/// stopped on its steering target while the waypoint does not yet count as reached - and path
	/// following deadlocks there, because nothing in that state can ever change.
	/// @param points The path to follow.
	/// @param waypointIndex The waypoint currently being headed for.
	/// @param position The bot's current position.
	/// @param acceptanceRadius Planar distance at which a steering target counts as reached.
	/// @param turnThresholdRadians Turn deviation below which a corner is not smoothed.
	/// @param maxTrimDistance How far a corner may be cut short at most.
	/// @return The waypoint to head for and the point to steer at.
	[[nodiscard]] BotPathFollowState AdvanceBotPathFollowing(
		const std::vector<Vector3>& points,
		std::size_t waypointIndex,
		const Vector3& position,
		float acceptanceRadius,
		float turnThresholdRadians,
		float maxTrimDistance);

	/// @brief Per-tick state the low level movement simulation carries between ticks.
	struct BotMovementRuntimeState final
	{
		Vector3 velocity { Vector3::Zero };
		Vector3 lastProgressPosition { Vector3::Zero };
		GameTime lastProgressTime { 0 };
		GameTime lastSimulationTime { 0 };
		GameTime lastHeartbeatTime { 0 };
		bool hasLastProgressPosition { false };
		bool isMoving { false };
	};

	/// @brief Input of a single low level movement tick.
	struct BotLowLevelMovementInput final
	{
		MovementInfo movement;
		BotMovementRuntimeState runtime;
		Vector3 steeringTarget { Vector3::Zero };
		GameTime now { 0 };
		float maxSpeed { 7.0f };
		float maxAcceleration { 40.48f };
		float acceptanceRadius { 0.75f };
	};

	/// @brief Result of a single low level movement tick.
	struct BotLowLevelMovementOutput final
	{
		MovementInfo movement;
		BotMovementRuntimeState runtime;
		bool reachedSteeringTarget { false };
		bool moved { false };
		float distanceToSteeringTarget { 0.0f };
	};

	/// @brief Accelerates the bot towards its steering target and integrates one tick of movement.
	///
	/// Standing on the steering target stops the bot dead, so callers must make sure the steering
	/// target they pass is one the bot is not already standing on - see AdvanceBotPathFollowing.
	[[nodiscard]] BotLowLevelMovementOutput AdvanceBotLowLevelMovement(const BotLowLevelMovementInput& input);
}
