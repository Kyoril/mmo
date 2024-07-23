// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "catch.hpp"

#include "game_server/game_unit_s.h"
#include "proto_data/project.h"

#include <memory>

using namespace mmo;

TEST_CASE("GetForwardVector points to positive x axis with facing 0", "[game_unit_s]")
{
	asio::io_service io{};
	TimerQueue timers{ io };
	proto::Project project{};
	std::shared_ptr<GameUnitS> unit = std::make_shared<GameUnitS>(project, timers);

	// Ensure unit is positioned and looking in the right direction
	MovementInfo movementInfo;
	movementInfo.position = Vector3(0.0f, 0.0f, 0.0f);
	movementInfo.facing = Radian(0.0f);
	movementInfo.fallTime = 0;
	movementInfo.movementFlags = movement_flags::None;
	movementInfo.timestamp = 0;
	unit->ApplyMovementInfo(movementInfo);

	// Ensure that a target away on the positive x-axis is considered to be in front of the unit
	const Vector3 forward = unit->GetForwardVector();
	REQUIRE(forward.IsNearlyEqual(Vector3::UnitX));
}

TEST_CASE("GetForwardVector points to negative x axis with facing Pi", "[game_unit_s]")
{
	asio::io_service io{};
	TimerQueue timers{ io };
	proto::Project project{};
	std::shared_ptr<GameUnitS> unit = std::make_shared<GameUnitS>(project, timers);

	// Ensure unit is positioned and looking in the right direction
	MovementInfo movementInfo;
	movementInfo.position = Vector3(0.0f, 0.0f, 0.0f);
	movementInfo.facing = Radian(Pi);
	movementInfo.fallTime = 0;
	movementInfo.movementFlags = movement_flags::None;
	movementInfo.timestamp = 0;
	unit->ApplyMovementInfo(movementInfo);

	// Ensure that a target away on the positive x-axis is considered to be in front of the unit
	const Vector3 forward = unit->GetForwardVector();
	REQUIRE(forward.IsNearlyEqual(Vector3::NegativeUnitX));
}

TEST_CASE("GetForwardVector points to positive z axis with facing Half Pi", "[game_unit_s]")
{
	asio::io_service io{};
	TimerQueue timers{ io };
	proto::Project project{};
	std::shared_ptr<GameUnitS> unit = std::make_shared<GameUnitS>(project, timers);

	// Ensure unit is positioned and looking in the right direction
	MovementInfo movementInfo;
	movementInfo.position = Vector3(0.0f, 0.0f, 0.0f);
	movementInfo.facing = Radian(Pi * 0.5f);
	movementInfo.fallTime = 0;
	movementInfo.movementFlags = movement_flags::None;
	movementInfo.timestamp = 0;
	unit->ApplyMovementInfo(movementInfo);

	// Ensure that a target away on the positive x-axis is considered to be in front of the unit
	const Vector3 forward = unit->GetForwardVector();
	REQUIRE(forward.IsNearlyEqual(Vector3::UnitZ));
}

TEST_CASE("IsFacingTowards returns true if target location is in front of unit", "[game_unit_s]")
{
	asio::io_service io{};
	TimerQueue timers{ io };
	proto::Project project{};
	std::shared_ptr<GameUnitS> unit = std::make_shared<GameUnitS>(project, timers);

	// Ensure unit is positioned and looking in the right direction
	MovementInfo movementInfo;
	movementInfo.position = Vector3(0.0f, 0.0f, 0.0f);
	movementInfo.facing = Radian(0.0f);
	movementInfo.fallTime = 0;
	movementInfo.movementFlags = movement_flags::None;
	movementInfo.timestamp = 0;
	unit->ApplyMovementInfo(movementInfo);
	REQUIRE(unit->GetPosition() == Vector3::Zero);
	REQUIRE(unit->GetFacing() == Radian(0.0f));

	// Ensure that a target away on the positive z-axis is considered to be in front of the unit
	bool isFacing = unit->IsFacingTowards(Vector3(1.0f, 0.0f, 0.0f));
	REQUIRE(isFacing);
}

TEST_CASE("IsInFront returns false if target location is not in front of unit", "[game_unit_s]")
{
	asio::io_service io{};
	TimerQueue timers{ io };
	proto::Project project{};
	std::shared_ptr<GameUnitS> unit = std::make_shared<GameUnitS>(project, timers);

	// Ensure unit is positioned and looking in the right direction
	MovementInfo movementInfo;
	movementInfo.position = Vector3(0.0f, 0.0f, 0.0f);
	movementInfo.facing = Radian(Pi);		// Pi means a 180° rotation, so we should be facing exactly in the opposite direction now!
	movementInfo.fallTime = 0;
	movementInfo.movementFlags = movement_flags::None;
	movementInfo.timestamp = 0;
	unit->ApplyMovementInfo(movementInfo);
	REQUIRE(unit->GetPosition() == Vector3::Zero);
	REQUIRE(unit->GetFacing() == Radian(Pi));

	// Ensure that a target away on the negative z-axis is considered to be in front of the unit
	REQUIRE(!unit->IsFacingTowards(Vector3(1.0f, 0.0f, 0.0f)));
}

TEST_CASE("IsFacingTowards returns true if target location is in front of unit with slight rotation", "[game_unit_s]")
{
	asio::io_service io{};
	TimerQueue timers{ io };
	proto::Project project{};
	std::shared_ptr<GameUnitS> unit = std::make_shared<GameUnitS>(project, timers);

	// Ensure unit is positioned and looking in the right direction
	MovementInfo movementInfo;
	movementInfo.position = Vector3(0.0f, 0.0f, 0.0f);
	movementInfo.facing = Radian(Pi / 6.0f);
	movementInfo.fallTime = 0;
	movementInfo.movementFlags = movement_flags::None;
	movementInfo.timestamp = 0;
	unit->ApplyMovementInfo(movementInfo);
	REQUIRE(unit->GetPosition() == Vector3::Zero);

	// Ensure that a target away on the negative z-axis is considered to be in front of the unit
	REQUIRE(unit->IsFacingTowards(Vector3(1.0f, 0.0f, 0.0f)));
}

TEST_CASE("IsFacingTowards returns false if target location is in side of unit with rotation", "[game_unit_s]")
{
	asio::io_service io{};
	TimerQueue timers{ io };
	proto::Project project{};
	std::shared_ptr<GameUnitS> unit = std::make_shared<GameUnitS>(project, timers);

	// Ensure unit is positioned and looking in the right direction
	MovementInfo movementInfo;
	movementInfo.position = Vector3(0.0f, 0.0f, 0.0f);
	movementInfo.facing = Radian(Pi / 2.0f);		// 90° rotation
	movementInfo.fallTime = 0;
	movementInfo.movementFlags = movement_flags::None;
	movementInfo.timestamp = 0;
	unit->ApplyMovementInfo(movementInfo);
	REQUIRE(unit->GetPosition() == Vector3::Zero);

	// Ensure that a target away on the negative z-axis is considered to be in front of the unit
	REQUIRE(!unit->IsFacingTowards(Vector3(1.0f, 0.0f, 0.0f)));
}

TEST_CASE("IsFacingTowards returns false if target location is in side of unit without unit rotation", "[game_unit_s]")
{
	asio::io_service io{};
	TimerQueue timers{ io };
	proto::Project project{};
	std::shared_ptr<GameUnitS> unit = std::make_shared<GameUnitS>(project, timers);

	// Ensure unit is positioned and looking in the right direction
	MovementInfo movementInfo;
	movementInfo.position = Vector3(0.0f, 0.0f, 0.0f);
	movementInfo.facing = Radian(0.0f);
	movementInfo.fallTime = 0;
	movementInfo.movementFlags = movement_flags::None;
	movementInfo.timestamp = 0;
	unit->ApplyMovementInfo(movementInfo);
	REQUIRE(unit->GetPosition() == Vector3::Zero);

	REQUIRE(!unit->IsFacingTowards(Vector3(0.0f, 0.0f, 1.0f)));
}
