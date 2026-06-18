// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_movement_controller.h"
#include "math/math_utils.h"

namespace mmo
{
	TEST_CASE("bot low level movement accelerates toward steering target", "[bot-movement]")
	{
		MovementInfo movement;
		movement.position = Vector3::Zero;
		movement.facing = Radian(0.0f);
		movement.movementFlags = movement_flags::Forward;

		BotMovementRuntimeState runtime;
		runtime.isMoving = true;
		runtime.lastSimulationTime = 1000;
		runtime.lastHeartbeatTime = 1000;

		BotLowLevelMovementInput input;
		input.movement = movement;
		input.runtime = runtime;
		input.steeringTarget = Vector3(10.0f, 0.0f, 0.0f);
		input.now = 1050;
		input.maxSpeed = 7.0f;
		input.maxAcceleration = 40.48f;
		input.acceptanceRadius = 0.25f;

		const BotLowLevelMovementOutput output = AdvanceBotLowLevelMovement(input);

		REQUIRE(output.moved);
		CHECK(output.movement.position.x == Approx(0.1012f).margin(0.0005f));
		CHECK(output.movement.position.z == Approx(0.0f).margin(0.0001f));
		CHECK(output.runtime.velocity.x == Approx(2.024f).margin(0.0005f));
		CHECK(output.movement.facing.GetValueRadians() == Approx(0.0f).margin(0.0001f));
		CHECK(output.runtime.lastSimulationTime == 1050);
		CHECK(output.runtime.hasLastProgressPosition);
	}

	TEST_CASE("bot low level movement emits client protocol facing", "[bot-movement]")
	{
		MovementInfo movement;
		movement.position = Vector3::Zero;
		movement.facing = Radian(0.0f);
		movement.movementFlags = movement_flags::Forward;

		BotMovementRuntimeState runtime;
		runtime.isMoving = true;
		runtime.velocity = Vector3(7.0f, 0.0f, 0.0f);
		runtime.lastSimulationTime = 1000;

		BotLowLevelMovementInput input;
		input.movement = movement;
		input.runtime = runtime;
		input.steeringTarget = Vector3(0.0f, 0.0f, -10.0f);
		input.now = 1050;
		input.maxSpeed = 7.0f;
		input.maxAcceleration = 100.0f;
		input.acceptanceRadius = 0.25f;

		const BotLowLevelMovementOutput output = AdvanceBotLowLevelMovement(input);

		REQUIRE(output.moved);
		CHECK(output.movement.facing.GetValueRadians() == Approx(Pi * 0.5f).margin(0.0001f));
	}

	TEST_CASE("bot low level movement clamps at steering target", "[bot-movement]")
	{
		MovementInfo movement;
		movement.position = Vector3(9.6f, 0.0f, 0.0f);
		movement.facing = Radian(0.0f);
		movement.movementFlags = movement_flags::Forward;

		BotMovementRuntimeState runtime;
		runtime.isMoving = true;
		runtime.velocity = Vector3(7.0f, 0.0f, 0.0f);
		runtime.lastSimulationTime = 1000;

		BotLowLevelMovementInput input;
		input.movement = movement;
		input.runtime = runtime;
		input.steeringTarget = Vector3(10.0f, 2.0f, 0.0f);
		input.now = 1100;
		input.maxSpeed = 7.0f;
		input.maxAcceleration = 40.48f;
		input.acceptanceRadius = 0.25f;

		const BotLowLevelMovementOutput output = AdvanceBotLowLevelMovement(input);

		CHECK(output.reachedSteeringTarget);
		CHECK(output.movement.position.x == Approx(10.0f).margin(0.0001f));
		CHECK(output.movement.position.y == Approx(2.0f).margin(0.0001f));
		CHECK(output.movement.position.z == Approx(0.0f).margin(0.0001f));
	}

	TEST_CASE("bot low level movement reports reached without advancing inside acceptance radius", "[bot-movement]")
	{
		MovementInfo movement;
		movement.position = Vector3(1.0f, 0.0f, 1.0f);
		movement.facing = Radian(0.0f);

		BotLowLevelMovementInput input;
		input.movement = movement;
		input.steeringTarget = Vector3(1.2f, 3.0f, 1.1f);
		input.now = 1000;
		input.acceptanceRadius = 0.5f;

		const BotLowLevelMovementOutput output = AdvanceBotLowLevelMovement(input);

		CHECK(output.reachedSteeringTarget);
		CHECK_FALSE(output.moved);
		CHECK(output.movement.position.x == Approx(1.0f).margin(0.0001f));
		CHECK(output.movement.position.y == Approx(0.0f).margin(0.0001f));
		CHECK(output.movement.position.z == Approx(1.0f).margin(0.0001f));
	}
}
