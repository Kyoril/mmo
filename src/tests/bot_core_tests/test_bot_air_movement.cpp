// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The headless bot's air physics: jumping, falling, gliding and updrafts, with the shared glide
// model the server validates against.

#include "catch.hpp"

#include "bot_core/bot_air_movement.h"
#include "bot_core/bot_updrafts.h"
#include "shared/proto_data/area_triggers.pb.h"

#include <cmath>

using namespace mmo;

namespace
{
	constexpr float kStep = 1.0f / 30.0f;
}

TEST_CASE("A bot jump rises, falls and lands at the landing height", "[bot_air]")
{
	BotAirMovement air;
	air.Jump(Vector3(0.0f, 10.0f, 0.0f), BotJumpSpeed, 10.0f);
	REQUIRE(air.IsAirborne());

	float peak = 10.0f;
	bool landed = false;
	BotAirMovement::Step step{};
	for (int i = 0; i < 30 && !landed; ++i)
	{
		step = air.Advance(kStep, {}, TraversalSettings());
		peak = std::max(peak, step.position.y);
		landed = step.landed;
	}

	CHECK(peak == Approx(10.0f + BotJumpSpeed * BotJumpSpeed / (2.0f * 19.6f)).margin(0.1f));
	CHECK(landed);
	CHECK(step.position.y == 10.0f);
	CHECK_FALSE(air.IsAirborne());
}

TEST_CASE("A gliding bot sinks at the glide sink speed", "[bot_air]")
{
	BotAirMovement air;
	air.Jump(Vector3(0.0f, 100.0f, 0.0f), BotJumpSpeed, -1000.0f);
	air.OpenGlider();
	REQUIRE(air.IsGliding());

	for (int i = 0; i < 90; ++i)
	{
		static_cast<void>(air.Advance(kStep, {}, TraversalSettings()));
	}

	CHECK(air.GetVelocity().y == Approx(-2.5f).margin(0.05f));
}

TEST_CASE("A gliding bot moves at the glide forward speed toward its direction", "[bot_air]")
{
	BotAirMovement air;
	air.Jump(Vector3(0.0f, 100.0f, 0.0f), BotJumpSpeed, -1000.0f);
	air.OpenGlider();
	air.SetGlideDirection(Vector3(1.0f, 0.0f, 0.0f));

	for (int i = 0; i < 15; ++i)
	{
		static_cast<void>(air.Advance(kStep, {}, TraversalSettings()));
	}

	const Vector3 velocity = air.GetVelocity();
	CHECK(std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z) == Approx(10.0f).margin(0.01f));
	CHECK(velocity.x > 0.0f);
}

TEST_CASE("An updraft lifts a gliding bot", "[bot_air]")
{
	UpdraftVolume updraft;
	updraft.center = Vector3(0.0f, 100.0f, 0.0f);
	updraft.radius = 20.0f;
	updraft.strength = 8.0f;

	BotAirMovement air;
	air.Jump(Vector3(0.0f, 100.0f, 0.0f), 0.0f, -1000.0f);
	air.OpenGlider();

	BotAirMovement::Step step{};
	for (int i = 0; i < 60; ++i)
	{
		step = air.Advance(kStep, { updraft }, TraversalSettings());
	}

	CHECK(step.position.y > 100.0f);
}

TEST_CASE("Bot updraft volumes follow the server's shape rules", "[bot_air]")
{
	proto::AreaTriggers triggers;

	auto* sphere = triggers.add_entry();
	sphere->set_map(2);
	sphere->set_x(1.0f);
	sphere->set_y(2.0f);
	sphere->set_z(3.0f);
	sphere->set_radius(6.0f);
	sphere->set_updraft_strength(8.0f);

	auto* box = triggers.add_entry();
	box->set_map(2);
	box->set_box_x(4.0f);
	box->set_box_y(6.0f);
	box->set_box_z(8.0f);
	box->set_box_o(1.0f);
	box->set_updraft_strength(5.0f);

	auto* weak = triggers.add_entry();
	weak->set_map(2);
	weak->set_radius(3.0f);

	auto* elsewhere = triggers.add_entry();
	elsewhere->set_map(1);
	elsewhere->set_radius(3.0f);
	elsewhere->set_updraft_strength(8.0f);

	const std::vector<UpdraftVolume> volumes = BuildBotUpdraftVolumes(triggers, 2);
	REQUIRE(volumes.size() == 2);
	CHECK(volumes[0].radius == Approx(6.0f));
	CHECK(volumes[0].strength == Approx(8.0f));
	CHECK(volumes[0].center.y == Approx(2.0f));
	CHECK(volumes[1].radius == Approx(0.0f));
	CHECK(volumes[1].halfExtents.x == Approx(2.0f));
	CHECK(volumes[1].halfExtents.y == Approx(3.0f));
	CHECK(volumes[1].halfExtents.z == Approx(4.0f));
	CHECK(volumes[1].orientation == Approx(1.0f));
}
