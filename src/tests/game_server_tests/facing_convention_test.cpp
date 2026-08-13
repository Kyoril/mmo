// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/objects/game_world_object_s.h"
#include "math/math_utils.h"
#include "shared/proto_data/project.h"

#include <cmath>
#include <memory>

using namespace mmo;

namespace
{
	constexpr float facingTolerance = 1e-4f;

	/// Builds the cheapest concrete GameObjectS available, since the facing helpers under test
	/// live on GameObjectS itself and do not care what kind of object they are called on.
	std::shared_ptr<GameWorldObjectS> MakeObject(proto::Project& project)
	{
		auto* entry = project.objects.add();
		entry->set_name("Facing Test Object");
		entry->set_type(game_world_object_type::Door);

		auto object = std::make_shared<GameWorldObjectS>(project, *entry);
		object->Initialize();

		return object;
	}
}

// GetForwardVector and GetAngle are the pair that drifted apart and caused mirrored AI
// positioning. They must stay exact inverses of each other: stepping forward along the
// object's own facing and then asking for the angle to that point has to give the facing
// back. A sign flip in either one breaks this even though each looks self consistent.
TEST_CASE("GetForwardVector and GetAngle are mutual inverses", "[facing][game_object_s]")
{
	proto::Project project;
	auto object = MakeObject(project);

	const float facings[] = { 0.0f, 0.3f, 1.0f, Pi * 0.5f, 2.0f, Pi, 4.0f, Pi * 1.5f, 6.0f };

	for (const float facing : facings)
	{
		object->Relocate(Vector3(10.0f, 5.0f, -20.0f), Radian(facing));

		// Walk one metre along where the object claims it is looking.
		const Vector3 ahead = object->GetPosition() + object->GetForwardVector();
		const Radian recovered = object->GetAngle(ahead.x, ahead.z);

		CHECK(std::fabs(recovered.GetValueRadians() - facing) <= facingTolerance);
	}
}

// GetAngle normalizes into [0, 2 * Pi), so a point behind the object on the -x axis must
// report Pi rather than -Pi. This pins the normalization half of the contract.
TEST_CASE("GetAngle normalizes into the positive range", "[facing][game_object_s]")
{
	proto::Project project;
	auto object = MakeObject(project);

	object->Relocate(Vector3::Zero, Radian(0.0f));

	CHECK(std::fabs(object->GetAngle(1.0f, 0.0f).GetValueRadians() - 0.0f) <= facingTolerance);
	CHECK(std::fabs(object->GetAngle(-1.0f, 0.0f).GetValueRadians() - Pi) <= facingTolerance);

	// A point on +z is a negative raw atan2 result, so it must wrap to 3/2 Pi rather than
	// staying at -1/2 Pi.
	CHECK(std::fabs(object->GetAngle(0.0f, 1.0f).GetValueRadians() - Pi * 1.5f) <= facingTolerance);
	CHECK(std::fabs(object->GetAngle(0.0f, -1.0f).GetValueRadians() - Pi * 0.5f) <= facingTolerance);
}

// Guards the specific mirroring that started this: forward at a quarter turn must be -z.
// If the convention is ever flipped back, this fails before anything reaches a player.
TEST_CASE("GetForwardVector follows the engine facing convention", "[facing][game_object_s]")
{
	proto::Project project;
	auto object = MakeObject(project);

	object->Relocate(Vector3::Zero, Radian(0.0f));
	CHECK(object->GetForwardVector().IsNearlyEqual(Vector3::UnitX, facingTolerance));

	object->Relocate(Vector3::Zero, Radian(Pi * 0.5f));
	CHECK(object->GetForwardVector().IsNearlyEqual(Vector3(0.0f, 0.0f, -1.0f), facingTolerance));

	object->Relocate(Vector3::Zero, Radian(Pi));
	CHECK(object->GetForwardVector().IsNearlyEqual(Vector3(-1.0f, 0.0f, 0.0f), facingTolerance));
}
