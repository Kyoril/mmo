// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "nav_build/rasterize.h"

#include "Recast.h"

#include <vector>

using namespace mmo;

namespace
{
	constexpr float CellSize = 0.25f;
	constexpr int WalkableClimb = 2;
	constexpr uint8 Walkable = 16;

	/// A heightfield over x, z in [0, 4] and y in [-1, 3], 0.25 units per voxel in every direction.
	struct Heightfield
	{
		rcContext context { false };
		rcHeightfield* field { rcAllocHeightfield() };

		Heightfield()
		{
			const float bmin[3] = { 0.0f, -1.0f, 0.0f };
			const float bmax[3] = { 4.0f, 3.0f, 4.0f };
			REQUIRE(rcCreateHeightfield(&context, *field, 16, 16, bmin, bmax, CellSize, CellSize));
		}

		~Heightfield()
		{
			rcFreeHeightField(field);
		}

		/// Area of the highest span in the column containing (x, z).
		uint8 TopArea(const float x, const float z) const
		{
			const int column = static_cast<int>(x / CellSize) + static_cast<int>(z / CellSize) * field->width;
			const rcSpan* span = field->spans[column];
			REQUIRE(span);
			while (span->next)
			{
				span = span->next;
			}
			return static_cast<uint8>(span->area);
		}
	};

	/// A floor over the whole field at y = 1.
	void Floor(std::vector<Vector3>& vertices, std::vector<int32>& indices)
	{
		vertices = { Vector3(0, 1, 0), Vector3(4, 1, 0), Vector3(4, 1, 4), Vector3(0, 1, 4) };
		indices = { 0, 1, 2, 0, 2, 3 };
	}

	/// A vertical face in the plane z = 2.1 reaching from y = 0 up to the floor, like the side of a
	/// platform or a stair riser meeting the floor above it flush.
	void Riser(std::vector<Vector3>& vertices, std::vector<int32>& indices)
	{
		vertices = { Vector3(0, 0, 2.1f), Vector3(4, 0, 2.1f), Vector3(4, 1, 2.1f), Vector3(0, 1, 2.1f) };
		indices = { 0, 1, 2, 0, 2, 3 };
	}
}

TEST_CASE("A floor meeting a vertical face flush stays walkable, whatever is rasterized first", "[nav_build]")
{
	std::vector<Vector3> floorVertices, riserVertices;
	std::vector<int32> floorIndices, riserIndices;
	Floor(floorVertices, floorIndices);
	Riser(riserVertices, riserIndices);

	SECTION("floor first")
	{
		Heightfield hf;
		REQUIRE(RasterizeNavTriangles(hf.context, *hf.field, 45.0f, floorVertices, floorIndices, Walkable, WalkableClimb));
		REQUIRE(RasterizeNavTriangles(hf.context, *hf.field, 45.0f, riserVertices, riserIndices, Walkable, WalkableClimb));
		CHECK(hf.TopArea(1.1f, 2.1f) == Walkable);
		CHECK(hf.TopArea(1.1f, 1.1f) == Walkable);
	}

	SECTION("riser first")
	{
		Heightfield hf;
		REQUIRE(RasterizeNavTriangles(hf.context, *hf.field, 45.0f, riserVertices, riserIndices, Walkable, WalkableClimb));
		REQUIRE(RasterizeNavTriangles(hf.context, *hf.field, 45.0f, floorVertices, floorIndices, Walkable, WalkableClimb));
		CHECK(hf.TopArea(1.1f, 2.1f) == Walkable);
	}
}

TEST_CASE("Steep and degenerate triangles are not walkable", "[nav_build]")
{
	Heightfield hf;

	// A 60 degree slope over z 0..4: steeper than the 45 degree limit
	const std::vector<Vector3> slope{ Vector3(0, 0, 0), Vector3(4, 0, 0), Vector3(4, 1.0f * 1.732f, 1), Vector3(0, 1.732f, 1) };
	const std::vector<int32> indices{ 0, 2, 1, 0, 3, 2 };
	REQUIRE(RasterizeNavTriangles(hf.context, *hf.field, 45.0f, slope, indices, Walkable, WalkableClimb));
	CHECK(hf.TopArea(1.1f, 0.6f) == RC_NULL_AREA);

	// Winding does not matter for walkability: the same floor wound both ways is walkable
	Heightfield flipped;
	const std::vector<Vector3> floor{ Vector3(0, 1, 0), Vector3(4, 1, 0), Vector3(4, 1, 4), Vector3(0, 1, 4) };
	const std::vector<int32> clockwise{ 0, 2, 1, 0, 3, 2 };
	REQUIRE(RasterizeNavTriangles(flipped.context, *flipped.field, 45.0f, floor, clockwise, Walkable, WalkableClimb));
	CHECK(flipped.TopArea(2.1f, 2.1f) == Walkable);
}
