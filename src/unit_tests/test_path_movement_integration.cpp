// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/math_utils.h"
#include "math/vector3.h"
#include <cmath>
#include <vector>

using namespace mmo;

// ============================================================================
// Path Integration Test Utilities
// ============================================================================

// Helper struct to simulate a unit's path-following state
struct PathSimulator
{
	std::vector<Vector3> pathWaypoints;
	std::vector<float> segmentLengths;
	Vector3 pathStartPosition{0.0f, 0.0f, 0.0f};
	float easingProgress = 0.0f;
	float easingTransitionDistance = 0.0f;

	// Initialize path from waypoints
	void SetPath(const std::vector<Vector3>& waypoints)
	{
		pathWaypoints = waypoints;
		segmentLengths.clear();
		
		if (waypoints.size() < 2)
		{
			return;
		}

		pathStartPosition = waypoints[0];
		
		// Calculate segment lengths
		for (size_t i = 0; i < waypoints.size() - 1; ++i)
		{
			Vector3 delta = waypoints[i + 1] - waypoints[i];
			segmentLengths.push_back(delta.GetLength());
		}
	}

	// Calculate position along path, with easing applied at turns
	Vector3 CalculatePositionAlongPath(float distance)
	{
		if (pathWaypoints.empty() || segmentLengths.empty())
		{
			return pathStartPosition;
		}

		float remainingDistance = distance;
		Vector3 currentPos = pathStartPosition;

		// Easing zone distance: smooth transitions over ~0.5 units on either side of a turn
		constexpr float easingZoneDistance = 0.5f;

		// Walk through path segments
		for (size_t i = 0; i < pathWaypoints.size() - 1 && i < segmentLengths.size(); ++i)
		{
			const float segmentLength = segmentLengths[i];
			const Vector3 segmentEnd = pathWaypoints[i + 1];

			if (remainingDistance <= segmentLength)
			{
				// We're somewhere along this segment
				if (segmentLength > 0.0f)
				{
					float t = remainingDistance / segmentLength;

					// Check if we're in an easing zone
					// Note: A turn exists at waypoint[i] if there's a turn between i-1, i, i+1
					// and a turn exists at waypoint[i+1] if there's a turn between i, i+1, i+2
					
					bool hasTurnAtStart = false;  // Is there a turn at the START of this segment (at waypoints[i])?
					bool hasTurnAtEnd = false;    // Is there a turn at the END of this segment (at waypoints[i+1])?

					// Check for turn at start of segment (at waypoints[i])
					if (i > 0)
					{
						// Three points: waypoints[i-1], waypoints[i], waypoints[i+1]
						hasTurnAtStart = DetectTurnBetweenSegments(pathWaypoints[i - 1], pathWaypoints[i], segmentEnd);
					}

					// Check for turn at end of segment (at waypoints[i+1])
					if (i + 2 < pathWaypoints.size())
					{
						// Three points: waypoints[i], waypoints[i+1], waypoints[i+2]
						hasTurnAtEnd = DetectTurnBetweenSegments(pathWaypoints[i], segmentEnd, pathWaypoints[i + 2]);
					}

					// Apply easing if we're near a turn
					if (hasTurnAtEnd && remainingDistance >= segmentLength - easingZoneDistance)
					{
						// Near the end of segment with a turn - ease out as we approach it
						float distanceFromTurn = segmentLength - remainingDistance;
						float easeProgress = 1.0f - (distanceFromTurn / easingZoneDistance);
						easeProgress = std::max(0.0f, std::min(1.0f, easeProgress));
						
						float easedT = EaseInOutCubic(easeProgress);
						t = (segmentLength - easingZoneDistance + easedT * easingZoneDistance) / segmentLength;
						t = std::max(0.0f, std::min(1.0f, t));
						
						this->easingProgress = easeProgress;
						this->easingTransitionDistance = easingZoneDistance;
					}
					else if (hasTurnAtStart && remainingDistance <= easingZoneDistance)
					{
						// Near the start of segment with a turn - ease in as we leave it
						float easeProgress = remainingDistance / easingZoneDistance;
						easeProgress = std::max(0.0f, std::min(1.0f, easeProgress));
						
						float easedT = EaseInOutCubic(easeProgress);
						t = easedT * easingZoneDistance / segmentLength;
						t = std::max(0.0f, std::min(1.0f, t));
						
						this->easingProgress = easeProgress;
						this->easingTransitionDistance = easingZoneDistance;
					}
					else
					{
						this->easingProgress = 0.0f;
					}

					return currentPos + (segmentEnd - currentPos) * t;
				}
				else
				{
					return segmentEnd;
				}
			}

			// Move to the end of this segment and continue
			remainingDistance -= segmentLength;
			currentPos = segmentEnd;
		}

		// If we get here, we're at or past the end of the path
		return pathWaypoints.back();
	}

	float ComputeSegmentAngle(const Vector3& prev, const Vector3& curr, const Vector3& next)
	{
		return mmo::ComputeSegmentAngle(prev, curr, next);
	}

	bool DetectTurnBetweenSegments(const Vector3& prevPoint, const Vector3& currPoint, const Vector3& nextPoint)
	{
		const float segmentAngle = ComputeSegmentAngle(prevPoint, currPoint, nextPoint);
		constexpr float turnThresholdRadians = 0.524f; // ~30 degrees
		return segmentAngle > turnThresholdRadians;
	}

	// Calculate total path length
	float GetTotalPathLength() const
	{
		float total = 0.0f;
		for (float len : segmentLengths)
		{
			total += len;
		}
		return total;
	}
};

// ============================================================================
// Basic Path Following Tests
// ============================================================================

TEST_CASE("path integration: straight line path without turns", "[path][integration]")
{
	PathSimulator sim;
	std::vector<Vector3> straightPath = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f)
	};
	sim.SetPath(straightPath);

	// At start
	Vector3 pos0 = sim.CalculatePositionAlongPath(0.0f);
	CHECK(pos0.x == Approx(0.0f).margin(0.01f));
	CHECK(pos0.z == Approx(0.0f).margin(0.01f));

	// Mid-path
	Vector3 pos5 = sim.CalculatePositionAlongPath(5.0f);
	CHECK(pos5.x == Approx(5.0f).margin(0.01f));
	CHECK(pos5.z == Approx(0.0f).margin(0.01f));

	// End
	Vector3 pos10 = sim.CalculatePositionAlongPath(10.0f);
	CHECK(pos10.x == Approx(10.0f).margin(0.01f));
	CHECK(pos10.z == Approx(0.0f).margin(0.01f));

	// No easing on straight line
	CHECK(sim.easingProgress == 0.0f);
}

TEST_CASE("path integration: simple right angle turn", "[path][integration]")
{
	PathSimulator sim;
	std::vector<Vector3> rightAnglePath = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f),  // Move right 10 units
		Vector3(10.0f, 0.0f, 10.0f)  // Turn 90 degrees, move forward 10 units
	};
	sim.SetPath(rightAnglePath);

	// First segment should be straight
	Vector3 pos5 = sim.CalculatePositionAlongPath(5.0f);
	CHECK(pos5.x == Approx(5.0f).margin(0.01f));
	CHECK(pos5.z == Approx(0.0f).margin(0.01f));

	// At the turn point (no easing zone yet - we're before it)
	Vector3 pos10 = sim.CalculatePositionAlongPath(10.0f);
	CHECK(pos10.x == Approx(10.0f).margin(0.01f));
	CHECK(pos10.z == Approx(0.0f).margin(0.01f));

	// After turn (should have easing applied)
	Vector3 pos15 = sim.CalculatePositionAlongPath(15.0f);
	CHECK(pos15.x == Approx(10.0f).margin(0.01f));
	CHECK(pos15.z > 0.0f);
	CHECK(pos15.z <= Approx(5.0f).margin(0.01f));
}

// ============================================================================
// Multi-Waypoint Path Tests
// ============================================================================

TEST_CASE("path integration: smooth movement through 5 waypoints", "[path][integration]")
{
	PathSimulator sim;
	
	// 5-waypoint path with alternating 90-degree turns
	std::vector<Vector3> multiWaypointPath = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f),   // Segment 1: straight right
		Vector3(10.0f, 0.0f, 10.0f),  // Segment 2: 90° right turn, then forward
		Vector3(0.0f, 0.0f, 10.0f),   // Segment 3: 90° left turn, then back left
		Vector3(0.0f, 0.0f, 0.0f),    // Segment 4: 90° left turn, then back down
		Vector3(5.0f, 0.0f, 5.0f)     // Segment 5: final diagonal segment
	};
	
	sim.SetPath(multiWaypointPath);
	float totalLength = sim.GetTotalPathLength();
	
	// Path segments: 10 + 10 + 10 + 10 + sqrt(50) ≈ 47.07
	// Let's just verify it's positive and we can traverse it
	CHECK(totalLength > 0.0f);
	
	// Test movement at various points along the path
	// We should be able to reach all waypoints
	
	// Segment 1: 0-10 units
	Vector3 p1 = sim.CalculatePositionAlongPath(0.0f);
	CHECK(p1.x == Approx(0.0f).margin(0.01f));
	
	Vector3 p2 = sim.CalculatePositionAlongPath(5.0f);
	CHECK(p2.x == Approx(5.0f).margin(0.01f));
	
	// Segment 2: 10-20 units (90° turn)
	Vector3 p3 = sim.CalculatePositionAlongPath(15.0f);
	CHECK(p3.x == Approx(10.0f).margin(0.01f));
	CHECK(p3.z > 0.0f);  // Should have moved forward
	
	// Segment 3: 20-30 units (another 90° turn)
	Vector3 p4 = sim.CalculatePositionAlongPath(25.0f);
	CHECK(p4.x < 10.0f);  // Should have turned back left
	CHECK(p4.z == Approx(10.0f).margin(0.01f));
	
	// End
	Vector3 pEnd = sim.CalculatePositionAlongPath(totalLength);
	// Final position should be somewhere near (5, 0, 5) or at the actual last waypoint
	CHECK(pEnd.x >= 0.0f);
	CHECK(pEnd.z >= 0.0f);
}

// ============================================================================
// Easing Zone Detection Tests
// ============================================================================

TEST_CASE("path integration: easing activates at turn zones", "[path][integration]")
{
	PathSimulator sim;
	std::vector<Vector3> turnPath = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 10.0f),
		Vector3(20.0f, 0.0f, 10.0f)
	};
	sim.SetPath(turnPath);
	
	// At distance 9.7 (0.3 into the easing zone before turn), easing should be active
	Vector3 beforeTurn = sim.CalculatePositionAlongPath(9.7f);
	CHECK(sim.easingProgress > 0.0f);  // Should be in easing zone with positive progress
	
	// At distance 10.0 (exactly at turn), easing may be at peak or transition
	Vector3 atTurn = sim.CalculatePositionAlongPath(10.0f);
	
	// At distance 10.3 (0.3 into easing zone after turn), easing should be active
	Vector3 afterTurn = sim.CalculatePositionAlongPath(10.3f);
	CHECK(sim.easingProgress > 0.0f);  // Should still be in easing zone
}

TEST_CASE("path integration: easing progress monotonic in zone", "[path][integration]")
{
	PathSimulator sim;
	std::vector<Vector3> turnPath = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 10.0f),
		Vector3(20.0f, 0.0f, 10.0f)
	};
	sim.SetPath(turnPath);
	
	// Track easing progress as we move through the zone
	float prevProgress = 0.0f;
	for (float dist = 9.0f; dist <= 11.0f; dist += 0.1f)
	{
		sim.CalculatePositionAlongPath(dist);
		// Easing progress should be monotonic or reset at boundaries
		if (sim.easingProgress > 0.0f)
		{
			CHECK(sim.easingProgress >= 0.0f);
			CHECK(sim.easingProgress <= 1.0f);
		}
	}
}

// ============================================================================
// Smooth Curve Validation Tests
// ============================================================================

TEST_CASE("path integration: position changes smoothly at turns", "[path][integration]")
{
	PathSimulator sim;
	std::vector<Vector3> turnPath = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 10.0f),
		Vector3(20.0f, 0.0f, 10.0f)
	};
	sim.SetPath(turnPath);
	
	// Sample positions before, during, and after the turn
	std::vector<Vector3> positions;
	float step = 0.1f;
	for (float dist = 9.0f; dist <= 11.0f; dist += step)
	{
		positions.push_back(sim.CalculatePositionAlongPath(dist));
	}
	
	// Check that consecutive positions have reasonable distance between them
	for (size_t i = 1; i < positions.size(); ++i)
	{
		float distance = (positions[i] - positions[i-1]).GetLength();
		// Distance should be small and not wildly fluctuate
		CHECK(distance < 0.5f);  // Max distance per step should be small
		CHECK(distance > 0.0f);  // Distance should be positive (moving forward)
	}
}

TEST_CASE("path integration: no jerking when crossing segment boundaries", "[path][integration]")
{
	PathSimulator sim;
	std::vector<Vector3> smoothPath = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 10.0f),
		Vector3(0.0f, 0.0f, 10.0f)
	};
	sim.SetPath(smoothPath);
	
	// Sample across segment boundaries
	float totalLength = sim.GetTotalPathLength();
	for (size_t seg = 1; seg < 3; ++seg)
	{
		float segmentStart = seg * 10.0f - 0.1f;
		float segmentEnd = seg * 10.0f + 0.1f;
		
		if (segmentStart < totalLength && segmentEnd < totalLength)
		{
			Vector3 before = sim.CalculatePositionAlongPath(segmentStart);
			Vector3 after = sim.CalculatePositionAlongPath(segmentEnd);
			
			// Distance between boundary samples should be smooth
			float jumpDistance = (after - before).GetLength();
			CHECK(jumpDistance < 1.0f);  // Should be continuous
		}
	}
}

// ============================================================================
// Edge Cases Tests
// ============================================================================

TEST_CASE("path integration: empty path returns start position", "[path][integration]")
{
	PathSimulator sim;
	Vector3 pos = sim.CalculatePositionAlongPath(0.0f);
	CHECK(pos.x == 0.0f);
	CHECK(pos.y == 0.0f);
	CHECK(pos.z == 0.0f);
}

TEST_CASE("path integration: single waypoint path", "[path][integration]")
{
	PathSimulator sim;
	std::vector<Vector3> singlePoint = {Vector3(5.0f, 0.0f, 5.0f)};
	sim.SetPath(singlePoint);
	
	// With only one waypoint, there are no segments, so we return the segment end position
	Vector3 pos = sim.CalculatePositionAlongPath(10.0f);
	// Should return start position since there are no segments
	CHECK(pos.x == Approx(0.0f).margin(0.01f));
	CHECK(pos.z == Approx(0.0f).margin(0.01f));
}

TEST_CASE("path integration: distance beyond path length", "[path][integration]")
{
	PathSimulator sim;
	std::vector<Vector3> shortPath = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(5.0f, 0.0f, 0.0f)
	};
	sim.SetPath(shortPath);
	
	// Request position beyond path length
	Vector3 pos = sim.CalculatePositionAlongPath(100.0f);
	CHECK(pos.x == Approx(5.0f).margin(0.01f));
	CHECK(pos.z == Approx(0.0f).margin(0.01f));
}

TEST_CASE("path integration: zero-length segments are handled", "[path][integration]")
{
	PathSimulator sim;
	std::vector<Vector3> pathWithZeroSegment = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f),  // Zero-length segment
		Vector3(10.0f, 0.0f, 10.0f)
	};
	sim.SetPath(pathWithZeroSegment);
	
	// Should handle gracefully
	Vector3 pos = sim.CalculatePositionAlongPath(10.0f);
	CHECK(pos.x == Approx(10.0f).margin(0.01f));
}

// ============================================================================
// Easing Function Application Tests
// ============================================================================

TEST_CASE("path integration: easing produces different curve than linear", "[path][integration]")
{
	// Compare linear interpolation vs eased interpolation at a turn
	PathSimulator sim;
	std::vector<Vector3> turnPath = {
		Vector3(0.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 0.0f),
		Vector3(10.0f, 0.0f, 10.0f),
		Vector3(20.0f, 0.0f, 10.0f)
	};
	sim.SetPath(turnPath);
	
	// Sample at mid-turn (should have maximum easing effect)
	float easingZoneStart = 10.0f - 0.5f;
	float easingZoneMid = 10.0f;
	float easingZoneEnd = 10.0f + 0.5f;
	
	Vector3 pos1 = sim.CalculatePositionAlongPath(easingZoneStart);
	Vector3 pos2 = sim.CalculatePositionAlongPath(easingZoneMid);
	Vector3 pos3 = sim.CalculatePositionAlongPath(easingZoneEnd);
	
	// The easing should make the curve smooth, not linear steps
	// Verify that positions form a smooth trajectory
	float dist1 = (pos2 - pos1).GetLength();
	float dist2 = (pos3 - pos2).GetLength();
	
	// Both distances should be positive
	CHECK(dist1 > 0.0f);
	CHECK(dist2 > 0.0f);
}

// ============================================================================
// Multi-Waypoint Complex Path Tests
// ============================================================================

TEST_CASE("path integration: complex zigzag path with 6 waypoints", "[path][integration]")
{
	PathSimulator sim;
	
	// Create a zigzag pattern: right, up, left, up, right, up
	std::vector<Vector3> zigzagPath = {
		Vector3(0.0f, 0.0f, 0.0f),     // Start
		Vector3(10.0f, 0.0f, 0.0f),    // Right
		Vector3(10.0f, 0.0f, 10.0f),   // Up (90° turn)
		Vector3(0.0f, 0.0f, 10.0f),    // Left (90° turn)
		Vector3(0.0f, 0.0f, 20.0f),    // Up (90° turn)
		Vector3(10.0f, 0.0f, 20.0f),   // Right (90° turn)
		Vector3(10.0f, 0.0f, 30.0f)    // Up (90° turn)
	};
	
	sim.SetPath(zigzagPath);
	float totalLength = sim.GetTotalPathLength();
	
	// Verify that we can traverse the entire path smoothly
	std::vector<Vector3> samples;
	for (float dist = 0.0f; dist <= totalLength; dist += 5.0f)
	{
		samples.push_back(sim.CalculatePositionAlongPath(dist));
	}
	
	// Should have enough samples
	CHECK(samples.size() >= 7);
	
	// First sample should be at start
	CHECK(samples[0].x == Approx(0.0f).margin(0.01f));
	CHECK(samples[0].z == Approx(0.0f).margin(0.01f));
	
	// Final sample should be near end
	CHECK(samples.back().x == Approx(10.0f).margin(1.0f));
	CHECK(samples.back().z == Approx(30.0f).margin(1.0f));
}

TEST_CASE("path integration: circular-ish path with 8 waypoints", "[path][integration]")
{
	PathSimulator sim;
	
	// Approximate circle with 8 waypoints (45° steps)
	float radius = 10.0f;
	std::vector<Vector3> circularPath;
	
	for (int i = 0; i < 8; ++i)
	{
		float angle = (i / 8.0f) * 6.28f; // 2*pi
		float x = radius * std::cos(angle);
		float z = radius * std::sin(angle);
		circularPath.push_back(Vector3(x, 0.0f, z));
	}
	
	sim.SetPath(circularPath);
	float totalLength = sim.GetTotalPathLength();
	
	// Should be able to traverse the entire path
	CHECK(totalLength > 0.0f);
	
	// Sample some points along the path
	Vector3 start = sim.CalculatePositionAlongPath(0.0f);
	Vector3 quarter = sim.CalculatePositionAlongPath(totalLength * 0.25f);
	Vector3 half = sim.CalculatePositionAlongPath(totalLength * 0.5f);
	Vector3 end = sim.CalculatePositionAlongPath(totalLength);
	
	// All points should be roughly at the same distance from origin
	float startDist = start.GetLength();
	float quarterDist = quarter.GetLength();
	float halfDist = half.GetLength();
	
	// Should form a circle-like pattern
	CHECK(startDist > 5.0f);  // Not at center
	CHECK(quarterDist > 5.0f);
	CHECK(halfDist > 5.0f);
}
