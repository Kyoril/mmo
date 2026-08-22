// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"
#include "scene_graph/scene.h"
#include "scene_graph/world_grid.h"

#include <vector>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	/// Records every position the grid asked about, so a test can see where the grid believes
	/// its own vertices are without needing to read back the geometry it built.
	struct RecordingProvider
	{
		std::vector<std::pair<float, float>> samples;
		float height { 5.0f };
		bool answer { true };

		WorldGrid::HeightProvider Bind()
		{
			return [this](const float x, const float z, float& outHeight)
			{
				samples.emplace_back(x, z);
				outHeight = height;
				return answer;
			};
		}

		[[nodiscard]] float MinX() const
		{
			float value = samples.front().first;
			for (const auto& [x, z] : samples) { value = std::min(value, x); }
			return value;
		}

		[[nodiscard]] float MaxX() const
		{
			float value = samples.front().first;
			for (const auto& [x, z] : samples) { value = std::max(value, x); }
			return value;
		}
	};
}

// The world grid is a flat plane, so it cuts through any ground not at its own height. Draping
// it means sampling a surface it cannot reach directly -- the grid lives in scene_graph and the
// terrain is built on top of that -- so the height comes in through a provider. These pin down
// that the provider is consulted only when asked for, and that it is asked in world space: the
// grid builds its geometry in node-local space and the node is snapped to the camera, so
// forgetting to add the node position is the mistake that would put the whole grid on the wrong
// piece of terrain.

TEST_CASE("The grid does not sample heights until it is told to follow the terrain", "[world_grid]")
{
	EnsureNullDevice();

	Scene scene;
	WorldGrid grid(scene, "GridNoFollow");

	RecordingProvider provider;
	grid.SetHeightProvider(provider.Bind());

	CHECK(grid.HasHeightProvider());
	CHECK_FALSE(grid.IsFollowingTerrain());

	grid.Update(Vector3::Zero);

	CHECK(provider.samples.empty());
}

TEST_CASE("A draping grid samples the surface", "[world_grid]")
{
	EnsureNullDevice();

	Scene scene;
	WorldGrid grid(scene, "GridFollow");

	RecordingProvider provider;
	grid.SetHeightProvider(provider.Bind());
	grid.SetFollowTerrain(true);
	grid.Update(Vector3::Zero);

	CHECK(grid.IsFollowingTerrain());
	CHECK_FALSE(provider.samples.empty());
}

TEST_CASE("A draping grid samples in world space, not grid-local space", "[world_grid]")
{
	EnsureNullDevice();

	Scene scene;
	WorldGrid grid(scene, "GridWorldSpace");

	RecordingProvider provider;
	grid.SetHeightProvider(provider.Bind());
	grid.SetFollowTerrain(true);

	// Build once at the origin to learn the grid's own extent.
	grid.Update(Vector3::Zero);
	const float originMinX = provider.MinX();
	const float originMaxX = provider.MaxX();

	CHECK(originMinX < 0.0f);
	CHECK(originMaxX > 0.0f);

	// Now move the camera far enough that the grid snaps to a new cell. The snap step is one
	// large-grid interval, which with the defaults is a full terrain page.
	const float snap = grid.GetGridSize() * grid.GetLargeGridInterval();

	provider.samples.clear();
	grid.Update(Vector3(snap, 0.0f, 0.0f));

	REQUIRE_FALSE(provider.samples.empty());

	// Every sample must have moved with the grid. If the node position were left out, the grid
	// would keep asking about the same patch of ground no matter where the camera went.
	CHECK(provider.MinX() == Approx(originMinX + snap));
	CHECK(provider.MaxX() == Approx(originMaxX + snap));
}

TEST_CASE("A stationary draping grid does not resample every frame", "[world_grid]")
{
	EnsureNullDevice();

	Scene scene;
	WorldGrid grid(scene, "GridStationary");

	RecordingProvider provider;
	grid.SetHeightProvider(provider.Bind());
	grid.SetFollowTerrain(true);
	grid.Update(Vector3::Zero);

	REQUIRE_FALSE(provider.samples.empty());

	// The geometry is only invalid once the grid moves or something says the ground changed.
	// Rebuilding it every frame would mean thousands of terrain lookups per frame for a grid
	// that has not moved.
	provider.samples.clear();
	grid.Update(Vector3(1.0f, 0.0f, 1.0f));
	CHECK(provider.samples.empty());

	// Deforming the terrain under a grid that has not moved is exactly the case that needs an
	// explicit nudge, since nothing about the grid itself changed.
	grid.InvalidateHeights();
	grid.Update(Vector3(1.0f, 0.0f, 1.0f));
	CHECK_FALSE(provider.samples.empty());
}

TEST_CASE("Grid detail controls how finely the surface is followed", "[world_grid]")
{
	EnsureNullDevice();

	Scene scene;
	WorldGrid grid(scene, "GridDetail");

	RecordingProvider provider;
	grid.SetHeightProvider(provider.Bind());
	grid.SetFollowTerrain(true);

	grid.SetTerrainSubdivisions(1);
	grid.Update(Vector3::Zero);
	const size_t coarse = provider.samples.size();

	provider.samples.clear();
	grid.SetTerrainSubdivisions(4);
	grid.Update(Vector3::Zero);
	const size_t fine = provider.samples.size();

	CHECK(coarse > 0);
	CHECK(fine > coarse);

	// One is the floor: a line still has to have two ends.
	provider.samples.clear();
	grid.SetTerrainSubdivisions(0);
	grid.Update(Vector3::Zero);
	CHECK(grid.GetTerrainSubdivisions() == 1);
}

TEST_CASE("A grid with no provider stays flat even when told to follow", "[world_grid]")
{
	EnsureNullDevice();

	Scene scene;
	WorldGrid grid(scene, "GridNoProvider");

	// The other editors share this class and have no terrain at all, so following must be a
	// no-op rather than a crash when nothing can answer.
	grid.SetFollowTerrain(true);
	grid.Update(Vector3::Zero);

	CHECK_FALSE(grid.HasHeightProvider());
	CHECK(grid.IsFollowingTerrain());
}

TEST_CASE("A provider that reports no surface is still consulted everywhere", "[world_grid]")
{
	EnsureNullDevice();

	Scene scene;
	WorldGrid grid(scene, "GridNoSurface");

	RecordingProvider provider;
	provider.answer = false;
	grid.SetHeightProvider(provider.Bind());
	grid.SetFollowTerrain(true);
	grid.Update(Vector3::Zero);

	// Terrain streams in, so "no surface here" is a temporary answer rather than a permanent
	// one. The grid must keep asking about those positions instead of writing them off.
	CHECK_FALSE(provider.samples.empty());
}

TEST_CASE("A grid built over terrain that had not streamed in retries by itself", "[world_grid]")
{
	EnsureNullDevice();

	Scene scene;
	WorldGrid grid(scene, "GridRetry");

	RecordingProvider provider;
	provider.answer = false;
	grid.SetHeightProvider(provider.Bind());
	grid.SetFollowTerrain(true);
	grid.Update(Vector3::Zero);

	REQUIRE_FALSE(provider.samples.empty());

	// Nothing announces a page arriving, so a grid that came out flat because the ground was not
	// resident would stay flat until the camera happened to cross into another cell. It retries
	// on a timer instead. Immediately afterwards is far too soon for that.
	provider.samples.clear();
	grid.Update(Vector3::Zero);
	CHECK(provider.samples.empty());

	// Once the ground answers, the retry that picks it up is also the last one: a complete build
	// leaves nothing to come back for.
	provider.answer = true;
	grid.InvalidateHeights();
	grid.Update(Vector3::Zero);
	REQUIRE_FALSE(provider.samples.empty());

	provider.samples.clear();
	grid.Update(Vector3::Zero);
	CHECK(provider.samples.empty());
}
