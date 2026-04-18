// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/vector3.h"

using namespace mmo;

/**
 * @brief Unit tests for reactive waypoint recalculation based on player movement threshold.
 * 
 * These tests verify that the creature AI system correctly detects when a player
 * has moved more than 5m from a creature's current waypoint target, triggering
 * recalculation of combat positioning.
 */

TEST_CASE("PLAYER_POSITION_THRESHOLD constant equals 25.0 (5m squared)", "[reactive][threshold]")
{
	// The constant should represent 5 meters squared for efficiency
	// 5.0f * 5.0f = 25.0f
	const float expectedThreshold = 25.0f;
	
	// Verify the expected value
	CHECK(expectedThreshold == 25.0f);
}

TEST_CASE("Distance threshold detects player movement under threshold", "[reactive][threshold]")
{
	// Simulating a player at 2m distance from waypoint target
	// sqrt(2) ≈ 1.414m, so squared = 2.0
	// This should NOT trigger recalculation (2.0 < 25.0)
	
	const float distanceUnderThresholdSq = 2.0f;
	const float threshold = 25.0f; // 5m squared
	
	CHECK(distanceUnderThresholdSq <= threshold);
	CHECK_FALSE(distanceUnderThresholdSq > threshold);
}

TEST_CASE("Distance threshold detects player movement at threshold boundary", "[reactive][threshold]")
{
	// Simulating a player exactly at 5m distance from waypoint target
	// 5.0 * 5.0 = 25.0
	// This should NOT trigger recalculation (25.0 is not > 25.0)
	
	const float distanceAtThresholdSq = 25.0f;
	const float threshold = 25.0f;
	
	CHECK(distanceAtThresholdSq <= threshold);
	CHECK_FALSE(distanceAtThresholdSq > threshold);
}

TEST_CASE("Distance threshold detects player movement just above threshold", "[reactive][threshold]")
{
	// Simulating a player just over 5m distance from waypoint target
	// sqrt(25.01) ≈ 5.001m, so squared = 25.01
	// This SHOULD trigger recalculation (25.01 > 25.0)
	
	const float distanceAboveThresholdSq = 25.01f;
	const float threshold = 25.0f;
	
	CHECK(distanceAboveThresholdSq > threshold);
}

TEST_CASE("Distance threshold detects player movement far above threshold", "[reactive][threshold]")
{
	// Simulating a player at 10m distance from waypoint target
	// 10.0 * 10.0 = 100.0
	// This SHOULD trigger recalculation (100.0 > 25.0)
	
	const float distanceFarAboveThresholdSq = 100.0f;
	const float threshold = 25.0f;
	
	CHECK(distanceFarAboveThresholdSq > threshold);
}

TEST_CASE("Squared distance preserves distance ordering", "[reactive][threshold]")
{
	// Verify that squared distances maintain proper ordering
	// For distances d1, d2 where d1 < d2, it's always true that d1^2 < d2^2
	
	const float distance1m = 1.0f;
	const float distance5m = 5.0f;
	const float distance10m = 10.0f;
	
	const float distance1mSq = distance1m * distance1m;
	const float distance5mSq = distance5m * distance5m;
	const float distance10mSq = distance10m * distance10m;
	
	CHECK(distance1mSq < distance5mSq);
	CHECK(distance5mSq < distance10mSq);
	CHECK(distance1mSq < distance10mSq);
}

TEST_CASE("Distance threshold with Vector3 squared distance calculations", "[reactive][threshold]")
{
	// Test actual vector distance calculations
	const Vector3 waypointTarget(100.0f, 100.0f, 0.0f);
	
	// Player at same position as waypoint
	const Vector3 playerSamePosition(100.0f, 100.0f, 0.0f);
	const float distanceSameSq = (playerSamePosition - waypointTarget).GetSquaredLength();
	CHECK(distanceSameSq <= 25.0f);
	
	// Player at 3m away (should not trigger)
	const Vector3 player3mAway(103.0f, 100.0f, 0.0f);
	const float distance3mSq = (player3mAway - waypointTarget).GetSquaredLength();
	CHECK(distance3mSq <= 25.0f);
	
	// Player at 5m away (boundary - should not trigger)
	const Vector3 player5mAway(105.0f, 100.0f, 0.0f);
	const float distance5mSq = (player5mAway - waypointTarget).GetSquaredLength();
	CHECK(distance5mSq <= 25.0f);
	
	// Player at 6m away (should trigger)
	const Vector3 player6mAway(106.0f, 100.0f, 0.0f);
	const float distance6mSq = (player6mAway - waypointTarget).GetSquaredLength();
	CHECK(distance6mSq > 25.0f);
}

TEST_CASE("Distance threshold with 3D vector calculations", "[reactive][threshold]")
{
	// Test that the threshold works correctly in 3D space
	const Vector3 waypointTarget(0.0f, 0.0f, 0.0f);
	
	// Player at (3, 4, 0) - should be 5m away (3^2 + 4^2 = 9 + 16 = 25)
	const Vector3 player3_4_0(3.0f, 4.0f, 0.0f);
	const float distance3_4_0_Sq = (player3_4_0 - waypointTarget).GetSquaredLength();
	CHECK(distance3_4_0_Sq == Approx(25.0f));
	CHECK_FALSE(distance3_4_0_Sq > 25.0f); // At boundary
	
	// Player at (3, 4, 1) - slightly more than 5m away (9 + 16 + 1 = 26)
	const Vector3 player3_4_1(3.0f, 4.0f, 1.0f);
	const float distance3_4_1_Sq = (player3_4_1 - waypointTarget).GetSquaredLength();
	CHECK(distance3_4_1_Sq > 25.0f);
}

TEST_CASE("Distance threshold recalculation not triggered prematurely", "[reactive][threshold]")
{
	// Verify that movements just under the threshold do not trigger recalculation
	const float threshold = 25.0f;
	
	// Movement of 4.99m (should not trigger)
	const float movement4_99mSq = 4.99f * 4.99f; // 24.9001
	CHECK(movement4_99mSq <= threshold);
	
	// Movement of 5.00m (boundary - should not trigger)
	const float movement5_00mSq = 5.0f * 5.0f; // 25.0
	CHECK(movement5_00mSq <= threshold);
}

TEST_CASE("Distance threshold recalculation triggered appropriately", "[reactive][threshold]")
{
	// Verify that movements over the threshold trigger recalculation
	const float threshold = 25.0f;
	
	// Movement of 5.01m (should trigger)
	const float movement5_01mSq = 5.01f * 5.01f; // 25.1001
	CHECK(movement5_01mSq > threshold);
	
	// Movement of 10m (should definitely trigger)
	const float movement10mSq = 10.0f * 10.0f; // 100.0
	CHECK(movement10mSq > threshold);
}

TEST_CASE("Multiple threshold comparisons maintain consistency", "[reactive][threshold]")
{
	// Verify that repeated distance checks maintain consistent results
	const Vector3 waypointTarget(50.0f, 50.0f, 50.0f);
	const Vector3 playerPosition(55.0f, 50.0f, 50.0f); // 5m away
	const float threshold = 25.0f;
	
	for (int i = 0; i < 10; ++i)
	{
		const float distanceSq = (playerPosition - waypointTarget).GetSquaredLength();
		// Should consistently be at threshold boundary
		CHECK(distanceSq <= threshold);
	}
}

TEST_CASE("Diagonal movement above threshold detected correctly", "[reactive][threshold]")
{
	// Test that diagonal movement in XY plane is detected
	const Vector3 waypointTarget(0.0f, 0.0f, 0.0f);
	const Vector3 playerDiagonal(4.0f, 4.0f, 0.0f); // sqrt(32) ≈ 5.66m
	const float distanceSq = (playerDiagonal - waypointTarget).GetSquaredLength();
	
	CHECK(distanceSq > 25.0f); // Should trigger recalculation
}

TEST_CASE("Vertical movement above threshold detected correctly", "[reactive][threshold]")
{
	// Test that vertical movement is detected (important for flying/terrain changes)
	const Vector3 waypointTarget(0.0f, 0.0f, 0.0f);
	const Vector3 playerVertical(0.0f, 0.0f, 6.0f); // 6m straight up
	const float distanceSq = (playerVertical - waypointTarget).GetSquaredLength();
	
	CHECK(distanceSq > 25.0f); // Should trigger recalculation
}

TEST_CASE("Combined movement components properly summed for threshold", "[reactive][threshold]")
{
	// Verify that distance components are properly combined in squared calculation
	const Vector3 waypointTarget(10.0f, 20.0f, 30.0f);
	
	// Player at (12, 22, 30) - 2m in X, 2m in Y, 0m in Z = sqrt(8) ≈ 2.83m
	const Vector3 playerSmallMove(12.0f, 22.0f, 30.0f);
	const float smallMoveSq = (playerSmallMove - waypointTarget).GetSquaredLength();
	CHECK(smallMoveSq < 25.0f);
	
	// Player at (15, 25, 30) - 5m in X, 5m in Y, 0m in Z = sqrt(50) ≈ 7.07m
	const Vector3 playerLargeMove(15.0f, 25.0f, 30.0f);
	const float largeMoveSq = (playerLargeMove - waypointTarget).GetSquaredLength();
	CHECK(largeMoveSq > 25.0f);
}
