// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "base/typedefs.h"
#include "terrain/brush_stroke.h"

#include <cmath>

using namespace mmo;
using namespace mmo::terrain;

TEST_CASE("BrushStroke_Stationary_Is_Radial_Distance", "[brush_stroke]")
{
	// A stationary brush has to behave exactly as the old point-radius brush did, since every
	// non-stroke caller builds one of these.
	const BrushStroke stroke = BrushStroke::At(10.0f, 20.0f);

	CHECK(stroke.DistanceTo(10.0f, 20.0f) == Approx(0.0f));
	CHECK(stroke.DistanceTo(13.0f, 24.0f) == Approx(5.0f));
	CHECK(stroke.DistanceTo(10.0f, 25.0f) == Approx(5.0f));
	CHECK(stroke.DistanceTo(2.0f, 20.0f) == Approx(8.0f));
}

TEST_CASE("BrushStroke_Perpendicular_Distance_Along_Segment", "[brush_stroke]")
{
	// This is what makes a stroke continuous: every point beside the segment is inside the
	// footprint, not just the points near its ends.
	const BrushStroke stroke{ 0.0f, 0.0f, 100.0f, 0.0f };

	for (const float x : { 0.0f, 1.0f, 25.0f, 50.0f, 99.0f, 100.0f })
	{
		CHECK(stroke.DistanceTo(x, 0.0f) == Approx(0.0f).margin(1e-4f));
		CHECK(stroke.DistanceTo(x, 3.0f) == Approx(3.0f).margin(1e-4f));
	}
}

TEST_CASE("BrushStroke_Ends_Stay_Round", "[brush_stroke]")
{
	// The projection is clamped, so the footprint is a capsule rather than an infinite band —
	// otherwise a stroke would paint past where the cursor actually stopped.
	const BrushStroke stroke{ 0.0f, 0.0f, 100.0f, 0.0f };

	CHECK(stroke.DistanceTo(-5.0f, 0.0f) == Approx(5.0f));
	CHECK(stroke.DistanceTo(105.0f, 0.0f) == Approx(5.0f));

	// Beyond an end, distance is measured from that end point, not from the axis.
	CHECK(stroke.DistanceTo(-3.0f, 4.0f) == Approx(5.0f));
	CHECK(stroke.DistanceTo(103.0f, 4.0f) == Approx(5.0f));
}

TEST_CASE("BrushStroke_Diagonal_Segment", "[brush_stroke]")
{
	const BrushStroke stroke{ 0.0f, 0.0f, 10.0f, 10.0f };

	// The midpoint of the segment.
	CHECK(stroke.DistanceTo(5.0f, 5.0f) == Approx(0.0f).margin(1e-4f));

	// Perpendicular offset from the midpoint: (1,-1) normalised times sqrt(2).
	CHECK(stroke.DistanceTo(6.0f, 4.0f) == Approx(std::sqrt(2.0f)).margin(1e-4f));
}

TEST_CASE("BrushStroke_Covers_Whole_Segment_Within_Radius", "[brush_stroke]")
{
	// The regression this whole change exists for: a fast drag from a far camera moves the
	// brush much further than its own diameter in one frame. A swept footprint must still
	// cover every point in between — that is what stops the stroke breaking into blobs.
	constexpr float radius = 8.0f;
	const BrushStroke stroke{ -400.0f, 120.0f, 260.0f, -35.0f };

	constexpr int32 samples = 512;
	for (int32 i = 0; i <= samples; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(samples);
		const float sampleX = stroke.fromX + (stroke.toX - stroke.fromX) * t;
		const float sampleZ = stroke.fromZ + (stroke.toZ - stroke.fromZ) * t;

		REQUIRE(stroke.DistanceTo(sampleX, sampleZ) <= radius);
	}
}

TEST_CASE("StrokePassFraction_Sums_To_One_Pass_However_The_Path_Is_Chopped", "[brush_stroke]")
{
	// The invariant the whole distance-driven model rests on: a point has 2 * radius worth of
	// path within reach of it, and however that path is split by frames or pointer events, the
	// contributions must add up to exactly one pass. Otherwise a stroke's darkness depends on
	// the frame rate and the cursor speed, which is what produced blotchy lines.
	constexpr float radius = 12.0f;
	constexpr float passLength = 2.0f * radius;

	for (const int32 pieces : { 1, 2, 3, 5, 8, 17, 64 })
	{
		const float pieceLength = passLength / static_cast<float>(pieces);

		float total = 0.0f;
		for (int32 i = 0; i < pieces; ++i)
		{
			total += StrokePassFraction(pieceLength, radius);
		}

		CHECK(total == Approx(1.0f).margin(1e-4f));
	}
}

TEST_CASE("StrokePassFraction_Caps_At_One_Full_Pass", "[brush_stroke]")
{
	constexpr float radius = 10.0f;

	// A segment longer than the brush is still only one pass over the points beneath it.
	CHECK(StrokePassFraction(20.0f, radius) == Approx(1.0f));
	CHECK(StrokePassFraction(500.0f, radius) == Approx(1.0f));
	CHECK(StrokePassFraction(50000.0f, radius) == Approx(1.0f));

	// Partial segments scale linearly.
	CHECK(StrokePassFraction(10.0f, radius) == Approx(0.5f));
	CHECK(StrokePassFraction(5.0f, radius) == Approx(0.25f));
}

TEST_CASE("StrokePassFraction_Degenerate_Inputs", "[brush_stroke]")
{
	// A stationary brush contributes nothing through the distance term; the caller falls back
	// to the time-driven amount instead.
	CHECK(StrokePassFraction(0.0f, 10.0f) == Approx(0.0f));
	CHECK(StrokePassFraction(-1.0f, 10.0f) == Approx(0.0f));

	// A degenerate radius must not divide by zero.
	CHECK(StrokePassFraction(5.0f, 0.0f) == Approx(0.0f));
	CHECK(StrokePassFraction(5.0f, -3.0f) == Approx(0.0f));
}

TEST_CASE("StrokePassFraction_Is_Speed_Independent", "[brush_stroke]")
{
	// Two cursors covering the same distance, one in a single frame and one in twenty, must
	// deposit the same total.
	constexpr float radius = 16.0f;
	constexpr float distance = 2.0f * radius;

	const float fast = StrokePassFraction(distance, radius);

	float slow = 0.0f;
	for (int32 i = 0; i < 20; ++i)
	{
		slow += StrokePassFraction(distance / 20.0f, radius);
	}

	CHECK(slow == Approx(fast).margin(1e-4f));
}

TEST_CASE("BrushStroke_Length_Matches_Segment", "[brush_stroke]")
{
	CHECK(BrushStroke::At(5.0f, 5.0f).Length() == Approx(0.0f));
	CHECK((BrushStroke{ 0.0f, 0.0f, 3.0f, 4.0f }).Length() == Approx(5.0f));
	CHECK((BrushStroke{ 3.0f, 4.0f, 0.0f, 0.0f }).Length() == Approx(5.0f));
}

TEST_CASE("BrushStroke_Bounds_Span_Both_Ends", "[brush_stroke]")
{
	// The brush templates derive their index range from these, so a wrong ordering would
	// silently clip the footprint to nothing for a stroke heading in the negative direction.
	const BrushStroke forward{ -10.0f, 5.0f, 30.0f, 45.0f };
	CHECK(forward.MinX() == Approx(-10.0f));
	CHECK(forward.MaxX() == Approx(30.0f));
	CHECK(forward.MinZ() == Approx(5.0f));
	CHECK(forward.MaxZ() == Approx(45.0f));

	const BrushStroke backward{ 30.0f, 45.0f, -10.0f, 5.0f };
	CHECK(backward.MinX() == Approx(-10.0f));
	CHECK(backward.MaxX() == Approx(30.0f));
	CHECK(backward.MinZ() == Approx(5.0f));
	CHECK(backward.MaxZ() == Approx(45.0f));
}
