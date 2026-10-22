// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/plaza_nameplate_layout.h"

#include <cmath>

using namespace mmo;

namespace
{
	constexpr float gap = 8.0f;
	constexpr float screenTop = 0.0f;
	constexpr float screenBottom = 2160.0f;

	PlazaNameplateBox makeBox(const int32 key, const float x, const float bottom, const float depth, const bool priority = false)
	{
		PlazaNameplateBox box;
		box.key = key;
		box.x = x;
		box.bottom = bottom;
		box.width = 400.0f;
		box.height = 120.0f;
		box.depth = depth;
		box.priority = priority;
		return box;
	}

	bool overlaps(const PlazaNameplateBox& a, const float offsetA, const PlazaNameplateBox& b, const float offsetB)
	{
		const float bottomA = a.bottom + offsetA;
		const float bottomB = b.bottom + offsetB;
		const bool horizontal = std::abs(a.x - b.x) < (a.width + b.width) * 0.5f;
		const bool vertical = bottomA - a.height < bottomB && bottomB - b.height < bottomA;
		return horizontal && vertical;
	}
}

TEST_CASE("Plates that do not overlap keep their anchors", "[plaza_nameplates]")
{
	const std::vector<PlazaNameplateBox> boxes{ makeBox(0, 500.0f, 1000.0f, 10.0f), makeBox(1, 1500.0f, 1000.0f, 12.0f) };
	const auto offsets = ResolvePlazaNameplateOffsets(boxes, gap, screenTop, screenBottom);
	CHECK(offsets[0] == 0.0f);
	CHECK(offsets[1] == 0.0f);
}

TEST_CASE("The farther plate of an overlapping pair moves up above the nearer one", "[plaza_nameplates]")
{
	// The far character's plate sits a little higher on screen and overlaps the near one.
	const std::vector<PlazaNameplateBox> boxes{ makeBox(0, 1000.0f, 950.0f, 14.0f), makeBox(1, 1100.0f, 1000.0f, 9.0f) };
	const auto offsets = ResolvePlazaNameplateOffsets(boxes, gap, screenTop, screenBottom);
	CHECK(offsets[1] == 0.0f);
	CHECK(offsets[0] < 0.0f);
	CHECK(boxes[0].bottom + offsets[0] == Approx(1000.0f - 120.0f - gap));
	CHECK_FALSE(overlaps(boxes[0], offsets[0], boxes[1], offsets[1]));
}

TEST_CASE("The selected plate keeps its place even when it is farther away", "[plaza_nameplates]")
{
	const std::vector<PlazaNameplateBox> boxes{ makeBox(0, 1000.0f, 950.0f, 14.0f, true), makeBox(1, 1100.0f, 1000.0f, 9.0f) };
	const auto offsets = ResolvePlazaNameplateOffsets(boxes, gap, screenTop, screenBottom);
	CHECK(offsets[0] == 0.0f);
	CHECK(offsets[1] != 0.0f);
	CHECK_FALSE(overlaps(boxes[0], offsets[0], boxes[1], offsets[1]));
}

TEST_CASE("A stack of overlapping plates ends up without overlaps", "[plaza_nameplates]")
{
	const std::vector<PlazaNameplateBox> boxes{
		makeBox(0, 1000.0f, 1000.0f, 8.0f),
		makeBox(1, 1050.0f, 990.0f, 10.0f),
		makeBox(2, 980.0f, 1010.0f, 12.0f),
		makeBox(3, 1120.0f, 960.0f, 14.0f),
		makeBox(4, 2000.0f, 1000.0f, 9.0f)
	};

	const auto offsets = ResolvePlazaNameplateOffsets(boxes, gap, screenTop, screenBottom);
	for (size_t a = 0; a < boxes.size(); ++a)
	{
		for (size_t b = a + 1; b < boxes.size(); ++b)
		{
			CHECK_FALSE(overlaps(boxes[a], offsets[a], boxes[b], offsets[b]));
		}
	}

	// The nearest plate and the one far off to the side stay where they are.
	CHECK(offsets[0] == 0.0f);
	CHECK(offsets[4] == 0.0f);
}

TEST_CASE("Plates stay on screen: no room above sends a plate below", "[plaza_nameplates]")
{
	// The near plate sits at the top edge: the farther one cannot go above it.
	const std::vector<PlazaNameplateBox> boxes{ makeBox(0, 1000.0f, 130.0f, 8.0f), makeBox(1, 1000.0f, 150.0f, 12.0f) };
	const auto offsets = ResolvePlazaNameplateOffsets(boxes, gap, screenTop, screenBottom);
	CHECK(offsets[0] == 0.0f);
	CHECK(boxes[1].bottom + offsets[1] - boxes[1].height >= screenTop);
	CHECK_FALSE(overlaps(boxes[0], offsets[0], boxes[1], offsets[1]));
}

TEST_CASE("A plate poking out at the top is pulled onto the screen", "[plaza_nameplates]")
{
	const std::vector<PlazaNameplateBox> boxes{ makeBox(0, 1000.0f, 60.0f, 8.0f) };
	const auto offsets = ResolvePlazaNameplateOffsets(boxes, gap, screenTop, screenBottom);
	CHECK(boxes[0].bottom + offsets[0] == Approx(120.0f));
}

TEST_CASE("The declutter eases towards the resolved offsets and does not jitter", "[plaza_nameplates]")
{
	PlazaNameplateDeclutter declutter;
	std::vector<PlazaNameplateBox> boxes{ makeBox(0, 500.0f, 1000.0f, 14.0f), makeBox(1, 1600.0f, 1000.0f, 9.0f) };

	// First sight: placed at once.
	auto offsets = declutter.Update(boxes, 0.016f, gap, screenTop, screenBottom);
	CHECK(offsets[0] == 0.0f);
	CHECK(offsets[1] == 0.0f);

	// The far character walks behind the near one: its plate glides up instead of jumping.
	boxes[0].x = 1550.0f;
	offsets = declutter.Update(boxes, 0.016f, gap, screenTop, screenBottom);
	const float target = 1000.0f - 120.0f - gap - 1000.0f;
	CHECK(offsets[0] < 0.0f);
	CHECK(offsets[0] > target);
	CHECK(offsets[1] == 0.0f);

	for (int frame = 0; frame < 120; ++frame)
	{
		offsets = declutter.Update(boxes, 0.016f, gap, screenTop, screenBottom);
	}
	CHECK(offsets[0] == Approx(target));

	// Standing still, nothing moves any more.
	const float settled = offsets[0];
	offsets = declutter.Update(boxes, 0.016f, gap, screenTop, screenBottom);
	CHECK(offsets[0] == settled);
}

TEST_CASE("The declutter forgets plates that disappear", "[plaza_nameplates]")
{
	PlazaNameplateDeclutter declutter;
	std::vector<PlazaNameplateBox> boxes{ makeBox(0, 1000.0f, 950.0f, 14.0f), makeBox(1, 1100.0f, 1000.0f, 9.0f) };
	(void)declutter.Update(boxes, 0.016f, gap, screenTop, screenBottom);

	// Plate 0 vanishes, then comes back alone: it starts at its (now zero) offset.
	(void)declutter.Update({ boxes[1] }, 0.016f, gap, screenTop, screenBottom);
	const auto offsets = declutter.Update({ boxes[0] }, 0.016f, gap, screenTop, screenBottom);
	CHECK(offsets[0] == 0.0f);
}
