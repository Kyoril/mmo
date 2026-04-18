// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/ai/creature_separation_math.h"
#include "math/vector3.h"
#include <cmath>

using namespace mmo;

// Helper function for comparing floats with tolerance
static bool FloatApproxEqual(float a, float b, float tolerance = 1e-5f)
{
	return std::fabs(a - b) <= tolerance;
}

// Helper function for comparing vectors with tolerance
static bool VectorApproxEqual(const Vector3& a, const Vector3& b, float tolerance = 1e-5f)
{
	return FloatApproxEqual(a.x, b.x, tolerance) &&
		   FloatApproxEqual(a.y, b.y, tolerance) &&
		   FloatApproxEqual(a.z, b.z, tolerance);
}

// ============================================================================
// Test Force Direction: Verify repulsion points away from target
// ============================================================================

TEST_CASE("Separation force points away from target on X-axis", "[separation][direction]")
{
	// Creature at origin, target to the right -> force should point left (negative X)
	Vector3 creaturePos(0.0f, 0.0f, 0.0f);
	Vector3 targetPos(1.0f, 0.0f, 0.0f);
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, 3.0f, 10.0f);
	
	// Force should point in negative X direction
	CHECK(force.x < 0.0f);
	CHECK(FloatApproxEqual(force.y, 0.0f));
	CHECK(FloatApproxEqual(force.z, 0.0f));
}

TEST_CASE("Separation force points away from target on Y-axis", "[separation][direction]")
{
	// Creature above target -> force should point upward (positive Y)
	Vector3 creaturePos(0.0f, 2.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, 3.0f, 10.0f);
	
	// Force should point in positive Y direction
	CHECK(FloatApproxEqual(force.x, 0.0f));
	CHECK(force.y > 0.0f);
	CHECK(FloatApproxEqual(force.z, 0.0f));
}

TEST_CASE("Separation force points away from target on Z-axis", "[separation][direction]")
{
	// Creature in front of target -> force should point forward (positive Z)
	Vector3 creaturePos(0.0f, 0.0f, 2.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, 3.0f, 10.0f);
	
	// Force should point in positive Z direction
	CHECK(FloatApproxEqual(force.x, 0.0f));
	CHECK(FloatApproxEqual(force.y, 0.0f));
	CHECK(force.z > 0.0f);
}

TEST_CASE("Separation force direction is opposite between creature pairs", "[separation][direction]")
{
	// Force from A away from B should be opposite to force from B away from A
	Vector3 posA(0.0f, 0.0f, 0.0f);
	Vector3 posB(2.0f, 0.0f, 0.0f);
	
	Vector3 forceA = CalculateSeparationForce(posA, posB, 3.0f, 10.0f);
	Vector3 forceB = CalculateSeparationForce(posB, posA, 3.0f, 10.0f);
	
	// Forces should be opposite (symmetry property)
	CHECK(VectorApproxEqual(forceA, -forceB));
}

TEST_CASE("Separation force in 3D space points correctly", "[separation][direction]")
{
	// Creature at (1,1,1), target at origin -> force should point in direction (1,1,1)
	Vector3 creaturePos(1.0f, 1.0f, 1.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, 3.0f, 1.0f);
	
	// All components should be positive and equal (pointing in diagonal direction)
	CHECK(force.x > 0.0f);
	CHECK(force.y > 0.0f);
	CHECK(force.z > 0.0f);
	
	// Direction should be along (1,1,1) normalized
	Vector3 expectedDirection(1.0f, 1.0f, 1.0f);
	expectedDirection.Normalize();
	
	// The force is normalized direction times some positive scalar
	float forceLength = force.GetLength();
	Vector3 forceDirection = force / forceLength;
	CHECK(VectorApproxEqual(forceDirection, expectedDirection));
}

// ============================================================================
// Test Force Magnitude: Verify magnitude varies correctly with distance
// ============================================================================

TEST_CASE("Separation force is maximum at zero distance", "[separation][magnitude]")
{
	// Identical positions should give zero distance, max force
	Vector3 pos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	// Use slightly offset positions to avoid zero-length direction
	Vector3 creaturePos(0.0001f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, magnitude);
	
	// Force length should be very close to magnitude (nearly max)
	float forceLength = force.GetLength();
	CHECK(forceLength > magnitude * 0.99f);  // Very close to maximum
}

TEST_CASE("Separation force at 0.5m with 3.0m threshold", "[separation][magnitude]")
{
	// At distance 0.5m with threshold 3.0m:
	// forceFactor = 1.0 - 0.5/3.0 = 1.0 - 0.1667 = 0.8333
	Vector3 creaturePos(0.5f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, magnitude);
	
	float expectedFactor = 1.0f - (0.5f / 3.0f);
	float expectedMagnitude = magnitude * expectedFactor;
	float forceLength = force.GetLength();
	
	CHECK(FloatApproxEqual(forceLength, expectedMagnitude, 1e-4f));
}

TEST_CASE("Separation force at 1.5m with 3.0m threshold", "[separation][magnitude]")
{
	// At distance 1.5m with threshold 3.0m:
	// forceFactor = 1.0 - 1.5/3.0 = 0.5
	Vector3 creaturePos(1.5f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, magnitude);
	
	float expectedMagnitude = magnitude * 0.5f;  // 5.0f
	float forceLength = force.GetLength();
	
	CHECK(FloatApproxEqual(forceLength, expectedMagnitude, 1e-4f));
}

TEST_CASE("Separation force at 2.9m with 3.0m threshold is near zero", "[separation][magnitude]")
{
	// At distance 2.9m with threshold 3.0m:
	// forceFactor = 1.0 - 2.9/3.0 = 0.0333
	Vector3 creaturePos(2.9f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, magnitude);
	
	float forceLength = force.GetLength();
	
	// Should be very small but non-zero
	CHECK(forceLength > 0.0f);
	CHECK(forceLength < magnitude * 0.05f);
}

TEST_CASE("Separation force is zero beyond threshold", "[separation][magnitude]")
{
	// Distance equal to or beyond threshold should give zero force
	Vector3 creaturePos(3.0f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, magnitude);
	
	// At distance exactly equal to threshold, force should be zero
	CHECK(FloatApproxEqual(force.x, 0.0f, 1e-5f));
	CHECK(FloatApproxEqual(force.y, 0.0f, 1e-5f));
	CHECK(FloatApproxEqual(force.z, 0.0f, 1e-5f));
}

TEST_CASE("Separation force is zero well beyond threshold", "[separation][magnitude]")
{
	// Distance far beyond threshold should give zero force
	Vector3 creaturePos(100.0f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, magnitude);
	
	CHECK(VectorApproxEqual(force, Vector3::Zero));
}

TEST_CASE("Separation force scales linearly with magnitude parameter", "[separation][magnitude]")
{
	// Force should be proportional to magnitude parameter
	Vector3 creaturePos(1.0f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	
	Vector3 force1 = CalculateSeparationForce(creaturePos, targetPos, threshold, 10.0f);
	Vector3 force2 = CalculateSeparationForce(creaturePos, targetPos, threshold, 20.0f);
	
	// Force2 should be approximately double Force1
	CHECK(VectorApproxEqual(force2, force1 * 2.0f, 1e-4f));
}

TEST_CASE("Separation force scales linearly with threshold parameter", "[separation][magnitude]")
{
	// At same absolute distance, different thresholds should give different forces
	Vector3 creaturePos(1.0f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float magnitude = 10.0f;
	
	// With threshold 2.0m at distance 1.0m: factor = 1.0 - 1.0/2.0 = 0.5
	Vector3 force1 = CalculateSeparationForce(creaturePos, targetPos, 2.0f, magnitude);
	
	// With threshold 4.0m at distance 1.0m: factor = 1.0 - 1.0/4.0 = 0.75
	Vector3 force2 = CalculateSeparationForce(creaturePos, targetPos, 4.0f, magnitude);
	
	// force2 should be stronger than force1
	CHECK(force2.GetLength() > force1.GetLength());
	
	// Verify the exact ratio
	float lengthRatio = force2.GetLength() / force1.GetLength();
	CHECK(FloatApproxEqual(lengthRatio, 0.75f / 0.5f, 1e-4f));  // ratio should be 1.5
}

// ============================================================================
// Test Zero-Vector Cases: Identical positions and beyond threshold
// ============================================================================

TEST_CASE("Separation force is zero when positions are identical", "[separation][zero]")
{
	// Identical positions -> zero direction, zero force
	Vector3 pos(5.0f, 3.0f, 2.0f);
	
	Vector3 force = CalculateSeparationForce(pos, pos, 3.0f, 10.0f);
	
	CHECK(VectorApproxEqual(force, Vector3::Zero));
}

TEST_CASE("Separation force returns zero vector when distance equals threshold", "[separation][zero]")
{
	Vector3 creaturePos(3.0f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, 10.0f);
	
	CHECK(VectorApproxEqual(force, Vector3::Zero));
}

TEST_CASE("Separation force returns zero vector when distance exceeds threshold", "[separation][zero]")
{
	Vector3 creaturePos(5.0f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, 10.0f);
	
	CHECK(VectorApproxEqual(force, Vector3::Zero));
}

// ============================================================================
// Test Multiple Creatures: Sum of forces produces balanced repulsion
// ============================================================================

TEST_CASE("Two creatures repel each other symmetrically", "[separation][multi]")
{
	// Two creatures at equal distance from center should repel symmetrically
	Vector3 creatureA(1.0f, 0.0f, 0.0f);
	Vector3 creatureB(-1.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 forceA = CalculateSeparationForce(creatureA, creatureB, threshold, magnitude);
	Vector3 forceB = CalculateSeparationForce(creatureB, creatureA, threshold, magnitude);
	
	// Forces should be equal in magnitude, opposite in direction
	CHECK(FloatApproxEqual(forceA.GetLength(), forceB.GetLength(), 1e-4f));
	CHECK(VectorApproxEqual(forceA, -forceB));
}

TEST_CASE("Three creatures in line balance forces", "[separation][multi]")
{
	// Three creatures in a line: A at 0, B at 1, C at 2
	// B should be pushed back by A and forward by C
	Vector3 creatureA(0.0f, 0.0f, 0.0f);
	Vector3 creatureB(1.0f, 0.0f, 0.0f);
	Vector3 creatureC(2.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 forceB_fromA = CalculateSeparationForce(creatureB, creatureA, threshold, magnitude);
	Vector3 forceB_fromC = CalculateSeparationForce(creatureB, creatureC, threshold, magnitude);
	Vector3 totalForceB = forceB_fromA + forceB_fromC;
	
	// A is at distance 1 from B, so factor = 1 - 1/3 = 2/3
	// C is at distance 1 from B, so factor = 1 - 1/3 = 2/3
	// ForceB_fromA should point right (+X), ForceB_fromC should point left (-X)
	// They should be equal in magnitude and opposite, so sum is near zero
	CHECK(FloatApproxEqual(totalForceB.GetLength(), 0.0f, 1e-4f));
}

TEST_CASE("Three creatures in triangle form balanced repulsion", "[separation][multi]")
{
	// Equilateral triangle of creatures
	Vector3 creatureA(0.0f, 0.0f, 0.0f);
	Vector3 creatureB(1.0f, 0.0f, 0.0f);
	Vector3 creatureC(0.5f, 0.866f, 0.0f);  // Approximately equilateral
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 forceA = CalculateSeparationForce(creatureA, creatureB, threshold, magnitude) +
					 CalculateSeparationForce(creatureA, creatureC, threshold, magnitude);
	Vector3 forceB = CalculateSeparationForce(creatureB, creatureA, threshold, magnitude) +
					 CalculateSeparationForce(creatureB, creatureC, threshold, magnitude);
	Vector3 forceC = CalculateSeparationForce(creatureC, creatureA, threshold, magnitude) +
					 CalculateSeparationForce(creatureC, creatureB, threshold, magnitude);
	
	// All forces should have similar magnitudes (balanced triangle)
	float forceALen = forceA.GetLength();
	float forceBLen = forceB.GetLength();
	float forceCLen = forceC.GetLength();
	
	// Forces should be roughly equal since triangle is symmetric
	CHECK(FloatApproxEqual(forceALen, forceBLen, 1e-3f));
	CHECK(FloatApproxEqual(forceBLen, forceCLen, 1e-3f));
}

TEST_CASE("Four creatures in square arrange predictably", "[separation][multi]")
{
	// Four creatures at corners of a 1x1 square
	Vector3 c1(0.0f, 0.0f, 0.0f);
	Vector3 c2(1.0f, 0.0f, 0.0f);
	Vector3 c3(1.0f, 1.0f, 0.0f);
	Vector3 c4(0.0f, 1.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	// Calculate force on c1 from all others
	Vector3 forceOnC1 = CalculateSeparationForce(c1, c2, threshold, magnitude) +
					   CalculateSeparationForce(c1, c3, threshold, magnitude) +
					   CalculateSeparationForce(c1, c4, threshold, magnitude);
	
	// Force should push c1 toward the corner (-X, -Y direction)
	CHECK(forceOnC1.x < 0.0f);
	CHECK(forceOnC1.y < 0.0f);
}

// ============================================================================
// Test Symmetry: force(A toward B) = -force(B toward A)
// ============================================================================

TEST_CASE("Separation forces are symmetric between two creatures", "[separation][symmetry]")
{
	Vector3 posA(0.0f, 0.0f, 0.0f);
	Vector3 posB(2.0f, 1.0f, 0.5f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 forceA = CalculateSeparationForce(posA, posB, threshold, magnitude);
	Vector3 forceB = CalculateSeparationForce(posB, posA, threshold, magnitude);
	
	// forceA should be equal to -forceB
	CHECK(VectorApproxEqual(forceA, -forceB, 1e-4f));
}

TEST_CASE("Separation force symmetry holds at various distances", "[separation][symmetry]")
{
	Vector3 posA(0.0f, 0.0f, 0.0f);
	Vector3 posB(0.5f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 forceA = CalculateSeparationForce(posA, posB, threshold, magnitude);
	Vector3 forceB = CalculateSeparationForce(posB, posA, threshold, magnitude);
	
	CHECK(VectorApproxEqual(forceA, -forceB, 1e-4f));
}

TEST_CASE("Separation force symmetry near threshold boundary", "[separation][symmetry]")
{
	Vector3 posA(0.0f, 0.0f, 0.0f);
	Vector3 posB(2.95f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	Vector3 forceA = CalculateSeparationForce(posA, posB, threshold, magnitude);
	Vector3 forceB = CalculateSeparationForce(posB, posA, threshold, magnitude);
	
	CHECK(VectorApproxEqual(forceA, -forceB, 1e-4f));
}

// ============================================================================
// Test Force Vector Properties
// ============================================================================

TEST_CASE("Separation force vector has unit length in direction", "[separation][properties]")
{
	// Direction component should always be normalized
	Vector3 creaturePos(1.0f, 2.0f, 3.0f);
	Vector3 targetPos(4.0f, 5.0f, 6.0f);
	float threshold = 10.0f;
	float magnitude = 1.0f;  // Use 1.0 to make magnitude equal to direction
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, magnitude);
	
	// Extract direction from force
	if (force.GetLength() > 1e-6f)
	{
		Vector3 direction = force / force.GetLength();
		
		// Should be normalized (length 1)
		CHECK(FloatApproxEqual(direction.GetLength(), 1.0f, 1e-5f));
	}
}

TEST_CASE("Separation force interpolates linearly between threshold boundaries", "[separation][properties]")
{
	Vector3 creaturePos(0.0f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 10.0f;
	
	// Test force at 0%, 25%, 50%, 75% of threshold distance
	Vector3 force0 = CalculateSeparationForce(Vector3(0.001f, 0.0f, 0.0f), targetPos, threshold, magnitude);
	Vector3 force25 = CalculateSeparationForce(Vector3(0.75f, 0.0f, 0.0f), targetPos, threshold, magnitude);
	Vector3 force50 = CalculateSeparationForce(Vector3(1.5f, 0.0f, 0.0f), targetPos, threshold, magnitude);
	Vector3 force75 = CalculateSeparationForce(Vector3(2.25f, 0.0f, 0.0f), targetPos, threshold, magnitude);
	
	float len0 = force0.GetLength();
	float len25 = force25.GetLength();
	float len50 = force50.GetLength();
	float len75 = force75.GetLength();
	
	// Should decrease monotonically
	CHECK(len0 > len25);
	CHECK(len25 > len50);
	CHECK(len50 > len75);
	
	// Check approximate ratios (should be linear)
	// At 25%: factor = 0.75, at 50%: factor = 0.5, at 75%: factor = 0.25
	CHECK(FloatApproxEqual(len25 / len0, 0.75f, 0.05f));
	CHECK(FloatApproxEqual(len50 / len0, 0.50f, 0.05f));
	CHECK(FloatApproxEqual(len75 / len0, 0.25f, 0.05f));
}

// ============================================================================
// Test Edge Cases and Robustness
// ============================================================================

TEST_CASE("Separation force with very small threshold still works", "[separation][edge]")
{
	Vector3 creaturePos(0.1f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 0.2f;
	float magnitude = 10.0f;
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, magnitude);
	
	// Should produce a valid force since distance (0.1) < threshold (0.2)
	CHECK(force.GetLength() > 0.0f);
	CHECK(force.x > 0.0f);
}

TEST_CASE("Separation force with very large magnitude", "[separation][edge]")
{
	Vector3 creaturePos(1.0f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 1000.0f;
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, magnitude);
	
	// Force magnitude should scale with the input magnitude
	float expectedLength = 1000.0f * (1.0f - 1.0f / 3.0f);
	CHECK(FloatApproxEqual(force.GetLength(), expectedLength, 1e-2f));
}

TEST_CASE("Separation force with zero force magnitude", "[separation][edge]")
{
	Vector3 creaturePos(1.0f, 0.0f, 0.0f);
	Vector3 targetPos(0.0f, 0.0f, 0.0f);
	float threshold = 3.0f;
	float magnitude = 0.0f;
	
	Vector3 force = CalculateSeparationForce(creaturePos, targetPos, threshold, magnitude);
	
	// With zero magnitude, result should be zero
	CHECK(VectorApproxEqual(force, Vector3::Zero));
}
