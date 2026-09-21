// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/fog_volume_math.h"
#include "deferred_shading/fog_volume_selection.h"

#include <cmath>

using namespace mmo;

TEST_CASE("Fog volume time-of-day factor", "[fog_volume]")
{
	CHECK(fog_volume::TimeOfDayFactor(13.0f, 0.0f, 0.0f, 1.0f) == Approx(1.0f));   // always on
	CHECK(fog_volume::TimeOfDayFactor(7.0f, 4.0f, 10.0f, 1.0f) == Approx(1.0f));
	CHECK(fog_volume::TimeOfDayFactor(12.0f, 4.0f, 10.0f, 1.0f) == Approx(0.0f));
	CHECK(fog_volume::TimeOfDayFactor(4.5f, 4.0f, 10.0f, 1.0f) == Approx(0.5f));   // fading in
	CHECK(fog_volume::TimeOfDayFactor(9.75f, 4.0f, 10.0f, 1.0f) == Approx(0.25f)); // fading out
	CHECK(fog_volume::TimeOfDayFactor(1.0f, 22.0f, 6.0f, 1.0f) == Approx(1.0f));   // across midnight
	CHECK(fog_volume::TimeOfDayFactor(23.0f, 22.0f, 6.0f, 1.0f) == Approx(1.0f));
	CHECK(fog_volume::TimeOfDayFactor(12.0f, 22.0f, 6.0f, 1.0f) == Approx(0.0f));
	CHECK(fog_volume::TimeOfDayFactor(5.0f, 4.0f, 6.0f, 3.0f) == Approx(1.0f));    // fade clamped to half the window
	CHECK(fog_volume::TimeOfDayFactor(7.0f, 4.0f, 10.0f, 0.0f) == Approx(1.0f));   // no fade
}

TEST_CASE("Fog volume shape distance honours yaw", "[fog_volume]")
{
	const Vector3 halfSize(10.0f, 2.0f, 4.0f);
	const float yawSin = std::sin(3.14159265f * 0.5f);   // 90 degrees
	const float yawCos = std::cos(3.14159265f * 0.5f);

	// A point 8 m along world +Z lies along the box's local long axis after a 90 degree yaw.
	const Vector3 local = fog_volume::ToLocal(Vector3(0.0f, 0.0f, 8.0f), Vector3(0.0f, 0.0f, 0.0f), yawSin, yawCos);
	CHECK(std::abs(local.x) == Approx(8.0f).margin(1e-4));
	CHECK(local.z == Approx(0.0f).margin(1e-4));
	CHECK(fog_volume::ShapeDistance(0, local, halfSize) == Approx(0.8f).margin(1e-4));

	CHECK(fog_volume::ShapeDistance(0, Vector3(5.0f, 1.0f, 2.0f), halfSize) == Approx(0.5f));
	CHECK(fog_volume::ShapeDistance(1, Vector3(5.0f, 1.0f, 2.0f), halfSize) == Approx(std::sqrt(0.75f)));
	CHECK(fog_volume::ShapeDistance(1, Vector3(10.0f, 0.0f, 0.0f), halfSize) == Approx(1.0f));
}

TEST_CASE("Fog volume edge fade and height factor", "[fog_volume]")
{
	CHECK(fog_volume::EdgeFade(0.0f, 0.3f) == Approx(1.0f));
	CHECK(fog_volume::EdgeFade(0.7f, 0.3f) == Approx(1.0f));
	CHECK(fog_volume::EdgeFade(1.0f, 0.3f) == Approx(0.0f));
	CHECK(fog_volume::EdgeFade(0.85f, 0.3f) == Approx(0.5f));
	CHECK(fog_volume::EdgeFade(0.99f, 0.0f) == Approx(1.0f));
	CHECK(fog_volume::EdgeFade(1.0f, 0.0f) == Approx(0.0f));

	const Vector3 halfSize(5.0f, 3.0f, 5.0f);
	CHECK(fog_volume::HeightFactor(Vector3(0.0f, -3.0f, 0.0f), halfSize, 0.1f) == Approx(1.0f));
	CHECK(fog_volume::HeightFactor(Vector3(0.0f, 0.0f, 0.0f), halfSize, 0.1f) == Approx(std::exp(-0.3f)));
}

TEST_CASE("Fog volume selection keeps active, near, visible volumes", "[fog_volume]")
{
	std::vector<FogVolume> volumes(4);
	volumes[0].position = Vector3(10.0f, 0.0f, 0.0f);
	volumes[1].position = Vector3(5.0f, 0.0f, 0.0f);
	volumes[2].position = Vector3(1000.0f, 0.0f, 0.0f);                 // beyond range
	volumes[3].position = Vector3(3.0f, 0.0f, 0.0f);
	volumes[3].activeFrom = 4.0f;                                        // inactive at noon
	volumes[3].activeTo = 10.0f;

	const auto all = [](const Vector3&, float) { return true; };
	const std::vector<FogVolumeInstance> selected = SelectFogVolumes(volumes, 12.0f, Vector3(0.0f, 0.0f, 0.0f), 200.0f, all);
	REQUIRE(selected.size() == 2);
	CHECK(selected[0].center.x == Approx(5.0f));   // nearest first
	CHECK(selected[1].center.x == Approx(10.0f));
	CHECK(selected[0].halfSize.x == Approx(10.0f));
	CHECK(selected[0].density == Approx(0.02f));

	const auto none = [](const Vector3&, float) { return false; };
	CHECK(SelectFogVolumes(volumes, 12.0f, Vector3(0.0f, 0.0f, 0.0f), 200.0f, none).empty());
}

TEST_CASE("Fog volume selection caps at the per-frame limit", "[fog_volume]")
{
	std::vector<FogVolume> volumes(100);
	for (size_t i = 0; i < volumes.size(); ++i)
	{
		volumes[i].position = Vector3(static_cast<float>(100 - i), 0.0f, 0.0f);
	}

	const auto all = [](const Vector3&, float) { return true; };
	const std::vector<FogVolumeInstance> selected = SelectFogVolumes(volumes, 12.0f, Vector3(0.0f, 0.0f, 0.0f), 500.0f, all);
	REQUIRE(selected.size() == fog_volume::MaxVolumesPerFrame);
	CHECK(selected.front().center.x == Approx(1.0f));
}

TEST_CASE("Fog volume selection keeps a large enclosing volume over farther small ones", "[fog_volume]")
{
	// A volume the camera stands inside has surface distance 0 no matter how far its centre is,
	// so it must never be pushed out of the MaxVolumesPerFrame cap by volumes whose centre is
	// nearer but whose surface is actually farther away. Regression test for sorting by distance
	// to the bounding sphere surface rather than to the centre.
	std::vector<FogVolume> volumes;
	volumes.reserve(1 + fog_volume::MaxVolumesPerFrame);

	FogVolume huge;
	huge.position = Vector3(1000.0f, 0.0f, 0.0f);
	huge.size = Vector3(3000.0f, 3000.0f, 3000.0f); // radius ~2598, camera at the origin is well inside.
	volumes.push_back(huge);

	for (uint32 i = 1; i <= fog_volume::MaxVolumesPerFrame; ++i)
	{
		FogVolume small;
		small.position = Vector3(static_cast<float>(i), 0.0f, 0.0f); // nearer centre than the huge volume.
		small.size = Vector3(0.1f, 0.1f, 0.1f);                      // radius ~0.087, camera stays outside.
		volumes.push_back(small);
	}

	const auto all = [](const Vector3&, float) { return true; };
	const std::vector<FogVolumeInstance> selected = SelectFogVolumes(volumes, 12.0f, Vector3(0.0f, 0.0f, 0.0f), 5000.0f, all);

	REQUIRE(selected.size() == fog_volume::MaxVolumesPerFrame);

	bool hugeVolumeKept = false;
	for (const FogVolumeInstance& instance : selected)
	{
		if (instance.center.x == Approx(1000.0f))
		{
			hugeVolumeKept = true;
		}
	}
	CHECK(hugeVolumeKept);

	// Its surface distance (0, camera inside) beats every small volume's positive surface
	// distance, so it also sorts first.
	CHECK(selected.front().center.x == Approx(1000.0f));
}

TEST_CASE("The GPU fog volume record is 80 bytes", "[fog_volume]")
{
	STATIC_REQUIRE(sizeof(fog_volume::GpuFogVolume) == 80);

	FogVolumeInstance instance;
	instance.center = Vector3(1.0f, 2.0f, 3.0f);
	instance.density = 0.5f;
	instance.shape = 1;
	const fog_volume::GpuFogVolume gpu = fog_volume::ToGpu(instance);
	CHECK(gpu.center[1] == Approx(2.0f));
	CHECK(gpu.density == Approx(0.5f));
	CHECK(gpu.shape == 1u);
}
