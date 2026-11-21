// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Server-side glide validation: the world server accepts a glide only while falling with the
// Glider and Vigor, limits how fast and how high a glider moves, and tolerates gliding packets for
// a short grace period after it ended the glide itself (ForceStopGlide).

#include "test_unit_factory.h"

#include "game_server/movement/glide_rules.h"
#include "game_server/objects/game_player_s.h"
#include "game_protocol/game_protocol.h"
#include "game/movement_info.h"
#include "game/traversal.h"
#include "shared/proto_data/area_triggers.pb.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <cmath>

using namespace mmo;
using namespace mmo::test;

namespace
{
	MovementInfo MakeInfo(const uint32 flags, const Vector3& position = Vector3::Zero)
	{
		MovementInfo info;
		info.movementFlags = flags;
		info.position = position;
		return info;
	}

	constexpr uint32 Falling = movement_flags::Falling;
	constexpr uint32 Gliding = movement_flags::Falling | movement_flags::Gliding;
}

TEST_CASE("Cannot start glide on the ground", "[glide]")
{
	CHECK(CanStartGlide(MakeInfo(movement_flags::None), true, 50.0f, false) == GlideStartDecision::NotFalling);
	CHECK(CanStartGlide(MakeInfo(movement_flags::Forward), true, 50.0f, false) == GlideStartDecision::NotFalling);
	CHECK(CanStartGlide(MakeInfo(Falling), true, 50.0f, false) == GlideStartDecision::Allowed);
}

TEST_CASE("Cannot start glide without Glider", "[glide]")
{
	CHECK(CanStartGlide(MakeInfo(Falling), false, 50.0f, false) == GlideStartDecision::NoGlider);
}

TEST_CASE("Cannot start glide at zero vigor", "[glide]")
{
	CHECK(CanStartGlide(MakeInfo(Falling), true, 0.0f, false) == GlideStartDecision::NoVigor);
	CHECK(CanStartGlide(MakeInfo(Falling), true, 0.1f, false) == GlideStartDecision::Allowed);
}

TEST_CASE("Cannot start glide while swimming, gliding, stunned or rooted", "[glide]")
{
	CHECK(CanStartGlide(MakeInfo(Falling | movement_flags::Swimming), true, 50.0f, false) == GlideStartDecision::Swimming);
	CHECK(CanStartGlide(MakeInfo(Gliding), true, 50.0f, false) == GlideStartDecision::AlreadyGliding);
	CHECK(CanStartGlide(MakeInfo(Falling), true, 50.0f, true) == GlideStartDecision::Incapacitated);
	CHECK(CanStartGlide(MakeInfo(Falling | movement_flags::Rooted), true, 50.0f, false) == GlideStartDecision::Incapacitated);
}

TEST_CASE("Only the glide opcodes change the Gliding flag", "[glide]")
{
	using namespace game::client_realm_packet;

	const MovementInfo falling = MakeInfo(Falling);
	const MovementInfo gliding = MakeInfo(Gliding);
	const MovementInfo ground = MakeInfo(movement_flags::None);

	CHECK(ClassifyGlideFlagChange(MoveStartGlide, falling, gliding) == GlideFlagChange::Start);
	CHECK(ClassifyGlideFlagChange(MoveStopGlide, gliding, falling) == GlideFlagChange::Stop);

	// Landing, entering water and the end of a server-controlled move end a glide implicitly.
	CHECK(ClassifyGlideFlagChange(MoveFallLand, gliding, ground) == GlideFlagChange::ImplicitStop);
	CHECK(ClassifyGlideFlagChange(MoveStartSwim, gliding, MakeInfo(movement_flags::Swimming)) == GlideFlagChange::ImplicitStop);
	CHECK(ClassifyGlideFlagChange(MoveEnded, gliding, ground) == GlideFlagChange::ImplicitStop);

	// Heartbeats keep the flag as it is.
	CHECK(ClassifyGlideFlagChange(MoveHeartBeat, gliding, gliding) == GlideFlagChange::Unchanged);
	CHECK(ClassifyGlideFlagChange(MoveHeartBeat, falling, falling) == GlideFlagChange::Unchanged);

	// A stop answering a ForceStopGlide after the client already stopped is harmless.
	CHECK(ClassifyGlideFlagChange(MoveStopGlide, falling, falling) == GlideFlagChange::Unchanged);

	// Any other way to add or remove the flag is not.
	CHECK(ClassifyGlideFlagChange(MoveHeartBeat, falling, gliding) == GlideFlagChange::Invalid);
	CHECK(ClassifyGlideFlagChange(MoveJump, ground, gliding) == GlideFlagChange::Invalid);
	CHECK(ClassifyGlideFlagChange(MoveHeartBeat, gliding, falling) == GlideFlagChange::Invalid);
	CHECK(ClassifyGlideFlagChange(MoveStartGlide, gliding, gliding) == GlideFlagChange::Invalid);
	CHECK(ClassifyGlideFlagChange(MoveStartGlide, falling, falling) == GlideFlagChange::Invalid);
	CHECK(ClassifyGlideFlagChange(MoveStopGlide, gliding, gliding) == GlideFlagChange::Invalid);

	// Gliding is only valid together with Falling.
	CHECK(ClassifyGlideFlagChange(MoveStartGlide, falling, MakeInfo(movement_flags::Gliding)) == GlideFlagChange::Invalid);
	CHECK(ClassifyGlideFlagChange(MoveHeartBeat, gliding, MakeInfo(movement_flags::Gliding)) == GlideFlagChange::Invalid);
}

TEST_CASE("Glide speed within tolerance passes, 15 m/s fails", "[glide]")
{
	const TraversalSettings settings;
	const Vector3 from(0.0f, 50.0f, 0.0f);

	CHECK(IsGlideSpeedValid(from, Vector3(10.0f, 47.5f, 0.0f), 1000, 0.0f, settings));
	CHECK(IsGlideSpeedValid(from, Vector3(0.0f, 47.5f, 13.4f), 1000, 0.0f, settings));
	CHECK_FALSE(IsGlideSpeedValid(from, Vector3(15.0f, 47.5f, 0.0f), 1000, 0.0f, settings));
	CHECK_FALSE(IsGlideSpeedValid(from, Vector3(7.5f, 47.5f, 0.0f), 500, 0.0f, settings));

	// Falling fast is not limited by the glide speed (the glider may have been opened mid-fall).
	CHECK(IsGlideSpeedValid(from, Vector3(5.0f, 20.0f, 0.0f), 500, 0.0f, settings));
}

TEST_CASE("Rising outside updraft fails, inside passes", "[glide]")
{
	const TraversalSettings settings;
	const Vector3 from(0.0f, 50.0f, 0.0f);

	CHECK_FALSE(IsGlideSpeedValid(from, Vector3(5.0f, 52.0f, 0.0f), 1000, 0.0f, settings));
	CHECK(IsGlideSpeedValid(from, Vector3(5.0f, 50.4f, 0.0f), 1000, 0.0f, settings));
	CHECK(IsGlideSpeedValid(from, Vector3(5.0f, 52.0f, 0.0f), 1000, settings.defaultUpdraftStrength, settings));

	// Even an updraft lifts only at its own strength.
	CHECK_FALSE(IsGlideSpeedValid(from, Vector3(5.0f, 70.0f, 0.0f), 1000, settings.defaultUpdraftStrength, settings));

	// Upward speed carried out of an updraft still lifts for a moment.
	const float carry = GetGlideRiseCarry(settings.defaultUpdraftStrength, settings);
	CHECK(carry == Approx(8.0f * 8.0f / (2.0f * settings.glideVerticalAccel)));
	CHECK(IsGlideSpeedValid(from, Vector3(5.0f, 52.0f, 0.0f), 1000, 0.0f, settings, carry));
	CHECK(GetGlideRiseCarry(-3.0f, settings) == Approx(0.0f));
}

TEST_CASE("Glide validator follows updraft exits and skips short intervals", "[glide]")
{
	const TraversalSettings settings;
	std::vector<UpdraftVolume> updrafts(1);
	updrafts[0].center = Vector3(0.0f, 50.0f, 0.0f);
	updrafts[0].radius = 5.0f;
	updrafts[0].strength = 8.0f;

	GlideMoveValidator validator;
	CHECK_FALSE(validator.IsActive());
	CHECK(validator.Check(Vector3(100.0f, 0.0f, 0.0f), 1000, updrafts) == GlideMoveValidator::Result::Skipped);

	validator.Begin(Vector3(0.0f, 48.0f, 0.0f), 1000, 0.0f, settings);
	REQUIRE(validator.IsActive());

	// Too short to measure: skipped, measured with the next one.
	CHECK(validator.Check(Vector3(0.5f, 48.5f, 0.0f), 1100, updrafts) == GlideMoveValidator::Result::Skipped);

	// Rising inside the updraft.
	CHECK(validator.Check(Vector3(2.0f, 51.0f, 0.0f), 1500, updrafts) == GlideMoveValidator::Result::Ok);

	// Leaving it at the top while still carrying upward speed.
	CHECK(validator.Check(Vector3(4.0f, 55.5f, 0.0f), 2000, updrafts) == GlideMoveValidator::Result::Ok);
	CHECK(validator.Check(Vector3(9.0f, 57.0f, 0.0f), 2500, updrafts) == GlideMoveValidator::Result::Ok);

	// The carried speed is used up: rising further is a violation.
	CHECK(validator.Check(Vector3(14.0f, 59.0f, 0.0f), 3000, updrafts) == GlideMoveValidator::Result::Violation);

	// Too fast.
	CHECK(validator.Check(Vector3(30.0f, 58.0f, 0.0f), 3500, updrafts) == GlideMoveValidator::Result::Violation);

	validator.End();
	CHECK_FALSE(validator.IsActive());
}

TEST_CASE("Glide opened during a jump may still rise, but only by a capped jump speed", "[glide]")
{
	const TraversalSettings settings;
	const std::vector<UpdraftVolume> noUpdrafts;

	GlideMoveValidator validator;
	validator.Begin(Vector3(0.0f, 10.0f, 0.0f), 1000, 7.0f, settings);
	CHECK(validator.Check(Vector3(3.0f, 12.0f, 0.0f), 1500, noUpdrafts) == GlideMoveValidator::Result::Ok);

	// A client claiming an absurd jump speed gets no more than the default updraft would give.
	GlideMoveValidator cheater;
	cheater.Begin(Vector3(0.0f, 10.0f, 0.0f), 1000, 500.0f, settings);
	CHECK(cheater.Check(Vector3(3.0f, 20.0f, 0.0f), 1500, noUpdrafts) == GlideMoveValidator::Result::Violation);
}

TEST_CASE("Glide past empty within grace is not a violation", "[glide]")
{
	const GameTime empty = 50000;
	CHECK(IsWithinEmptyGrace(empty, empty));
	CHECK(IsWithinEmptyGrace(empty + 900, empty));
	CHECK_FALSE(IsWithinEmptyGrace(empty + 1100, empty));

	// Packets timed before the stop are always fine.
	CHECK(IsWithinEmptyGrace(empty - 500, empty));
}

TEST_CASE("Fall height restarts at glide stop", "[glide]")
{
	// The glide stopped 3 m above the ground: the fall is measured from there, so no damage.
	CHECK(ComputeFallDamagePercent(3.0f, 0.0f, 5.0f, 40.0f) == Approx(0.0f));

	// Without the restart the same landing after a glide from 60 m would have been lethal.
	CHECK(ComputeFallDamagePercent(60.0f, 0.0f, 5.0f, 40.0f) == Approx(1.0f));

	// Linear between the minimum and the lethal height.
	CHECK(ComputeFallDamagePercent(22.5f, 0.0f, 5.0f, 40.0f) == Approx(0.5f));
	CHECK(ComputeFallDamagePercent(105.0f, 100.0f, 5.0f, 40.0f) == Approx(0.0f));

	// Landing higher than the start (an updraft carried the glider up) is no fall at all.
	CHECK(ComputeFallDamagePercent(0.0f, 10.0f, 5.0f, 40.0f) == Approx(0.0f));

	// A broken configuration (lethal not above minimum) never divides by zero.
	CHECK(ComputeFallDamagePercent(20.0f, 0.0f, 5.0f, 5.0f) == Approx(0.0f));
}

TEST_CASE("Only refused glide starts no honest client sends are violations", "[glide]")
{
	// Temporary states the client may not know of yet: the glider is closed, nothing more.
	CHECK_FALSE(IsGlideStartRefusalViolation(GlideStartDecision::NoVigor));
	CHECK_FALSE(IsGlideStartRefusalViolation(GlideStartDecision::Incapacitated));
	CHECK_FALSE(IsGlideStartRefusalViolation(GlideStartDecision::NotFalling));

	CHECK(IsGlideStartRefusalViolation(GlideStartDecision::NoGlider));
	CHECK(IsGlideStartRefusalViolation(GlideStartDecision::Swimming));
	CHECK(IsGlideStartRefusalViolation(GlideStartDecision::AlreadyGliding));

	CHECK_FALSE(IsGlideStartRefusalViolation(GlideStartDecision::Allowed));
}

TEST_CASE("Free fall speed and equivalent height are inverse", "[glide]")
{
	CHECK(GetFreeFallSpeed(10.0f) == Approx(std::sqrt(2.0f * glide_rules::FallGravity * 10.0f)));
	CHECK(GetEquivalentFallHeight(GetFreeFallSpeed(10.0f)) == Approx(10.0f));
	CHECK(GetFreeFallSpeed(-3.0f) == Approx(0.0f));
	CHECK(GetEquivalentFallHeight(-5.0f) == Approx(0.0f));

	// Terminal speed caps very long falls.
	CHECK(GetFreeFallSpeed(10000.0f) == Approx(glide_rules::TerminalFallSpeed));
}

TEST_CASE("Last-moment glide open still takes fall damage", "[glide]")
{
	const TraversalSettings settings;

	// Fell 59 m from 60 m, opened the glider 1 m above the ground and landed 0.1 s later.
	GlideFallModel model;
	model.Begin(60.0f, 1.0f, 1000);
	model.Advance(1100, 0.0f, settings);

	const float withGlider = ComputeFallDamagePercent(model.GetVirtualFallStartY(0.0f), 0.0f, 5.0f, 40.0f);
	const float withoutGlider = ComputeFallDamagePercent(60.0f, 0.0f, 5.0f, 40.0f);
	CHECK(withGlider > 0.0f);
	CHECK(withGlider >= 0.9f * withoutGlider);

	// Measured without the lethal clamp, the 0.1 s of braking saves only a little height.
	CHECK(model.GetVirtualFallStartY(0.0f) > 50.0f);
	CHECK(model.GetVirtualFallStartY(0.0f) < 60.0f);
}

TEST_CASE("Long glide landing takes no fall damage", "[glide]")
{
	const TraversalSettings settings;

	// Opened from rest (at the top of a jump) and glided for 5 s: the glider sinks at its sink speed.
	GlideFallModel model;
	model.Begin(80.0f, 80.0f, 1000);
	model.Advance(6000, 0.0f, settings);
	CHECK(model.GetVerticalSpeed() == Approx(-settings.glideSinkSpeed));

	CHECK(ComputeFallDamagePercent(model.GetVirtualFallStartY(67.5f), 67.5f, 5.0f, 40.0f) == Approx(0.0f));

	// A glider opened in a fast fall has braked to its sink speed after 5 s as well.
	GlideFallModel fast;
	fast.Begin(100.0f, 50.0f, 1000);
	fast.Advance(6000, 0.0f, settings);
	CHECK(fast.GetVerticalSpeed() == Approx(-settings.glideSinkSpeed));
	CHECK(ComputeFallDamagePercent(fast.GetVirtualFallStartY(20.0f), 20.0f, 5.0f, 40.0f) == Approx(0.0f));
}

TEST_CASE("Glide stop 3 m above ground takes no damage", "[glide]")
{
	const TraversalSettings settings;

	GlideFallModel model;
	model.Begin(50.0f, 50.0f, 1000);
	model.Advance(4000, 0.0f, settings);

	// The fall after the stop is measured from the virtual start height above the stop.
	const float fallStartY = model.GetVirtualFallStartY(3.0f);
	CHECK(ComputeFallDamagePercent(fallStartY, 0.0f, 5.0f, 40.0f) == Approx(0.0f));
}

TEST_CASE("Glide stop high above ground restarts at sink speed", "[glide]")
{
	const TraversalSettings settings;

	GlideFallModel model;
	model.Begin(150.0f, 120.0f, 1000);
	model.Advance(6000, 0.0f, settings);

	// Sinking slowly, the fall restarts practically at the stop height ...
	const float fallStartY = model.GetVirtualFallStartY(100.0f);
	CHECK(fallStartY == Approx(100.0f + GetEquivalentFallHeight(settings.glideSinkSpeed)));
	CHECK(fallStartY - 100.0f < 0.5f);

	// ... and a fall from there to the ground is as deadly as any other.
	CHECK(ComputeFallDamagePercent(fallStartY, 0.0f, 5.0f, 40.0f) == Approx(1.0f));

	// Rising in an updraft, the stop is a fall from rest.
	model.Advance(9000, settings.defaultUpdraftStrength, settings);
	CHECK(model.GetVerticalSpeed() > 0.0f);
	CHECK(model.GetVirtualFallStartY(100.0f) == Approx(100.0f));
}

TEST_CASE("Updraft volumes are built from the map's area triggers", "[glide]")
{
	proto::AreaTriggers triggers;

	auto* sphere = triggers.add_entry();
	sphere->set_id(1);
	sphere->set_name("Sphere updraft");
	sphere->set_map(3);
	sphere->set_x(10.0f);
	sphere->set_y(20.0f);
	sphere->set_z(30.0f);
	sphere->set_radius(6.0f);
	sphere->set_updraft_strength(9.0f);

	auto* box = triggers.add_entry();
	box->set_id(2);
	box->set_name("Box updraft");
	box->set_map(3);
	box->set_x(0.0f);
	box->set_y(0.0f);
	box->set_z(0.0f);
	box->set_box_x(4.0f);
	box->set_box_y(20.0f);
	box->set_box_z(8.0f);
	box->set_box_o(0.5f);
	box->set_updraft_strength(5.0f);

	auto* plain = triggers.add_entry();
	plain->set_id(3);
	plain->set_name("Not an updraft");
	plain->set_map(3);
	plain->set_x(0.0f);
	plain->set_y(0.0f);
	plain->set_z(0.0f);
	plain->set_radius(100.0f);

	auto* otherMap = triggers.add_entry();
	otherMap->set_id(4);
	otherMap->set_name("Other map");
	otherMap->set_map(4);
	otherMap->set_x(0.0f);
	otherMap->set_y(0.0f);
	otherMap->set_z(0.0f);
	otherMap->set_radius(5.0f);
	otherMap->set_updraft_strength(5.0f);

	const std::vector<UpdraftVolume> volumes = BuildUpdraftVolumes(triggers, 3);
	REQUIRE(volumes.size() == 2);

	CHECK(volumes[0].radius == Approx(6.0f));
	CHECK(volumes[0].strength == Approx(9.0f));
	CHECK(volumes[0].center.y == Approx(20.0f));

	// box_x/y/z are the full size, like the area-trigger containment test reads them.
	CHECK(volumes[1].radius == Approx(0.0f));
	CHECK(volumes[1].halfExtents.x == Approx(2.0f));
	CHECK(volumes[1].halfExtents.y == Approx(10.0f));
	CHECK(volumes[1].halfExtents.z == Approx(4.0f));
	CHECK(volumes[1].orientation == Approx(0.5f));

	CHECK(GetUpdraftStrength(volumes, Vector3(10.0f, 22.0f, 30.0f)) == Approx(9.0f));
	CHECK(GetUpdraftStrength(volumes, Vector3(0.0f, 9.0f, 0.0f)) == Approx(5.0f));
	CHECK(GetUpdraftStrength(volumes, Vector3(0.0f, 11.0f, 0.0f)) == Approx(0.0f));
}

TEST_CASE("Teleport clears glide", "[glide]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto* glider = project.spells.add(traversal_spells::Glider);
	glider->set_name("Glider");
	glider->add_attributes(0);
	glider->add_attributes(0);

	auto player = MakeUnit(project, timers);
	player->Set<uint64>(object_fields::Guid, 1);
	player->AddSpell(traversal_spells::Glider);

	player->ApplyMovementInfo(MakeInfo(Gliding, Vector3(0.0f, 50.0f, 0.0f)));
	player->SetVigorGliding(true, false);
	REQUIRE(player->GetVigor().IsGliding());
	REQUIRE(player->GetMovementInfo().IsGliding());

	player->TeleportOnMap(Vector3(100.0f, 0.0f, 100.0f), Radian(0.0f));

	CHECK_FALSE(player->GetVigor().IsGliding());
	CHECK_FALSE(player->GetMovementInfo().IsGliding());

	// Ending the glide directly (logout, map change) is the same.
	player->ApplyMovementInfo(MakeInfo(Gliding, Vector3(0.0f, 50.0f, 0.0f)));
	player->SetVigorGliding(true, false);
	player->EndGlide();
	CHECK_FALSE(player->GetVigor().IsGliding());
	CHECK_FALSE(player->GetMovementInfo().IsGliding());
}
