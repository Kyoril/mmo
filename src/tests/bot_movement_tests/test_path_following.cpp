// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_movement_math.h"

#include <vector>

using namespace mmo;

// Mirrors the defaults in BotMovementSettings.
namespace
{
	constexpr float waypointAcceptanceRadius = 0.75f;
	constexpr float turnSmoothingThresholdRadians = 0.524f;
	constexpr float turnSmoothingDistance = 0.5f;
	constexpr float maxAcceleration = 40.48f;
	constexpr float runSpeed = 7.0f;

	// The non-progress watchdog in BotMovementController::Update.
	constexpr float progressDistanceEpsilon = 0.15f;
	constexpr GameTime nonProgressTimeoutMs = 3000;

	/// The nav mesh path the crypt_wing_bosses scenario walks: from where the scenario ports in
	/// (-16, 1, 4) to Ossuar at (-19, 1, 14) on map 1. Captured from a real run; it turns through
	/// two doorways, and the corners at index 2 (59 degrees) and index 4 (32 degrees) are sharp
	/// enough to be smoothed.
	std::vector<Vector3> CryptApproachPath()
	{
		return {
			Vector3(-16.0f, 1.0591f, 3.90625f),
			Vector3(-13.8027f, 1.0591f, 3.90625f),
			Vector3(-12.5007f, 1.0591f, 4.6875f),
			Vector3(-12.5007f, 1.0591f, 5.98958f),
			Vector3(-12.7611f, 1.0591f, 7.29167f),
			Vector3(-14.8041f, 1.0591f, 9.48845f),
			Vector3(-16.8472f, 1.0591f, 11.6852f),
			Vector3(-19.0f, 1.0591f, 14.0f),
		};
	}

	struct FollowSimulation final
	{
		bool stalled { false };
		bool reachedEnd { false };
		Vector3 stallPosition { Vector3::Zero };
		std::size_t stallWaypointIndex { 0 };
	};

	/// Walks a bot along `path` the way BotMovementController::Update does - resolve the waypoint
	/// and steering target, then integrate one tick - and reports whether the bot ever went as
	/// still as the controller's non-progress watchdog requires to give up.
	FollowSimulation SimulateFollow(const std::vector<Vector3>& path, const GameTime tickMs)
	{
		FollowSimulation result;

		BotLowLevelMovementInput input;
		input.movement.position = path.front();
		input.maxSpeed = runSpeed;
		input.maxAcceleration = maxAcceleration;
		input.acceptanceRadius = waypointAcceptanceRadius;

		std::size_t waypointIndex = path.size() > 1 ? 1u : 0u;
		GameTime now = 0;
		Vector3 lastProgressPosition = input.movement.position;
		GameTime lastProgressTime = 0;

		// Generous: the path is ~13 units long and the bot runs it at 7 units per second.
		const GameTime simulationLimitMs = 60000;
		while (now < simulationLimitMs)
		{
			now += tickMs;

			const BotPathFollowState follow = AdvanceBotPathFollowing(
				path, waypointIndex, input.movement.position,
				waypointAcceptanceRadius, turnSmoothingThresholdRadians, turnSmoothingDistance);
			waypointIndex = follow.waypointIndex;
			if (follow.exhausted)
			{
				result.reachedEnd = true;
				return result;
			}

			input.steeringTarget = follow.steeringTarget;
			input.now = now;

			const BotLowLevelMovementOutput output = AdvanceBotLowLevelMovement(input);
			input.movement = output.movement;
			input.runtime = output.runtime;

			if (PlanarDistance(lastProgressPosition, input.movement.position) >= progressDistanceEpsilon)
			{
				lastProgressPosition = input.movement.position;
				lastProgressTime = now;
				continue;
			}

			if (now - lastProgressTime >= nonProgressTimeoutMs)
			{
				result.stalled = true;
				result.stallPosition = input.movement.position;
				result.stallWaypointIndex = waypointIndex;
				return result;
			}
		}

		return result;
	}
}

TEST_CASE("path following: a bot standing on a smoothed corner target passes that waypoint", "[bot][path]")
{
	// Regression: acceptance used to be measured against the raw waypoint while the bot steered
	// at - and stopped dead on - the smoothed target that sits short of it. Standing in the band
	// between the two radii, the bot could neither move (it was on its steering target) nor
	// advance (it was not on the waypoint), and froze there until the watchdog fired. This is the
	// exact geometry and position of one of those freezes in crypt_wing_bosses.
	const std::vector<Vector3> path = CryptApproachPath();
	const Vector3 stuck(-13.2283f, 1.05248f, 4.41052f);

	const Vector3 steeringTarget = ComputeSmoothedPathTarget(
		path, 2, turnSmoothingThresholdRadians, turnSmoothingDistance);

	// The corner really is smoothed, so the two candidate acceptance points differ.
	REQUIRE(PlanarDistance(steeringTarget, path[2]) > 0.0f);

	// The bot has stopped: it is on its steering target...
	REQUIRE(PlanarDistance(stuck, steeringTarget) <= waypointAcceptanceRadius);
	// ...while the raw waypoint is still out of reach. Measuring acceptance there is the bug.
	REQUIRE(PlanarDistance(stuck, path[2]) > waypointAcceptanceRadius);

	const BotPathFollowState follow = AdvanceBotPathFollowing(
		path, 2, stuck, waypointAcceptanceRadius, turnSmoothingThresholdRadians, turnSmoothingDistance);

	CHECK(follow.waypointIndex == 3);
	CHECK_FALSE(follow.exhausted);
}

TEST_CASE("path following: the crypt approach never stalls, whatever the tick rate", "[bot][path]")
{
	// The freeze only bit when a tick happened to land inside the band, which made it a coin flip
	// on tick size - the flake was roughly one crypt_wing_bosses run in four. Every tick rate the
	// scenario runner can produce has to walk the whole path.
	const std::vector<Vector3> path = CryptApproachPath();

	for (const GameTime tickMs : { 1u, 2u, 4u, 8u, 11u, 16u, 22u, 33u })
	{
		const FollowSimulation simulation = SimulateFollow(path, tickMs);

		INFO("tick " << tickMs << " ms, stalled at waypoint " << simulation.stallWaypointIndex
			<< " (" << simulation.stallPosition.x << ", " << simulation.stallPosition.z << ")");
		CHECK_FALSE(simulation.stalled);
		CHECK(simulation.reachedEnd);
	}
}

TEST_CASE("path following: a straight path steers at its waypoints unchanged", "[bot][path]")
{
	// Nothing to smooth, so the steering target must be the waypoint itself - the fix must not
	// start cutting corners that were never corners.
	const std::vector<Vector3> path = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(0.0f, 0.0f, 5.0f),
		Vector3(0.0f, 0.0f, 10.0f),
		Vector3(0.0f, 0.0f, 15.0f),
	};

	const BotPathFollowState follow = AdvanceBotPathFollowing(
		path, 1, Vector3(0.0f, 0.0f, 0.0f),
		waypointAcceptanceRadius, turnSmoothingThresholdRadians, turnSmoothingDistance);

	CHECK(follow.waypointIndex == 1);
	CHECK(follow.steeringTarget.z == Approx(5.0f));
}

TEST_CASE("path following: standing on several waypoints skips all of them", "[bot][path]")
{
	// A path that doubles back on itself can leave the bot inside the acceptance radius of more
	// than one waypoint; it must come out heading for the first one it is not standing on.
	const std::vector<Vector3> path = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(0.0f, 0.0f, 0.2f),
		Vector3(0.0f, 0.0f, 0.4f),
		Vector3(0.0f, 0.0f, 8.0f),
	};

	const BotPathFollowState follow = AdvanceBotPathFollowing(
		path, 1, Vector3(0.0f, 0.0f, 0.0f),
		waypointAcceptanceRadius, turnSmoothingThresholdRadians, turnSmoothingDistance);

	CHECK(follow.waypointIndex == 3);
	CHECK_FALSE(follow.exhausted);
}

TEST_CASE("path following: reports exhaustion once the last waypoint is reached", "[bot][path]")
{
	const std::vector<Vector3> path = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(0.0f, 0.0f, 5.0f),
	};

	const BotPathFollowState follow = AdvanceBotPathFollowing(
		path, 1, Vector3(0.0f, 0.0f, 4.9f),
		waypointAcceptanceRadius, turnSmoothingThresholdRadians, turnSmoothingDistance);

	CHECK(follow.exhausted);
	CHECK(follow.waypointIndex == path.size());
}
