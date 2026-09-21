// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_common/fog_volume.h"
#include "game_common/world_fog_volumes.h"

#include "binary_io/memory_source.h"
#include "binary_io/vector_sink.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <vector>

using namespace mmo;

namespace
{
	std::vector<char> writeVolumes(const std::vector<FogVolume>& volumes)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		WorldFogVolumeSerializer::Write(writer, volumes);
		return buffer;
	}

	bool readVolumes(const std::vector<char>& buffer, std::vector<FogVolume>& out)
	{
		io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
		io::Reader reader{ source };
		WorldFogVolumeDeserializer deserializer{ out };
		return deserializer.Read(reader);
	}
}

TEST_CASE("Fog volumes round-trip through the .hfog format", "[fog_volumes]")
{
	FogVolume box;
	box.id = 3;
	box.name = "Pond mist";
	box.shape = FogVolumeShape::Box;
	box.position = Vector3(10.0f, 2.0f, -4.0f);
	box.size = Vector3(30.0f, 5.0f, 12.0f);
	box.yaw = 35.0f;
	box.density = 0.05f;
	box.color = Vector3(0.8f, 1.2f, 0.7f);
	box.edgeFade = 0.5f;
	box.heightFalloff = 0.2f;
	box.activeFrom = 4.0f;
	box.activeTo = 10.0f;
	box.fadeHours = 1.5f;
	box.noiseAmount = 0.6f;
	box.noiseDetail = 2;

	FogVolume ellipsoid;
	ellipsoid.id = 9;
	ellipsoid.shape = FogVolumeShape::Ellipsoid;
	ellipsoid.position = Vector3(-1.0f, 0.0f, 1.0f);

	std::vector<FogVolume> loaded;
	REQUIRE(readVolumes(writeVolumes({ box, ellipsoid }), loaded));
	REQUIRE(loaded.size() == 2);

	const FogVolume& a = loaded[0];
	CHECK(a.id == 3);
	CHECK(a.name == "Pond mist");
	CHECK(a.shape == FogVolumeShape::Box);
	CHECK(a.position.x == Approx(10.0f));
	CHECK(a.size.z == Approx(12.0f));
	CHECK(a.yaw == Approx(35.0f));
	CHECK(a.density == Approx(0.05f));
	CHECK(a.color.y == Approx(1.2f));
	CHECK(a.edgeFade == Approx(0.5f));
	CHECK(a.heightFalloff == Approx(0.2f));
	CHECK(a.activeFrom == Approx(4.0f));
	CHECK(a.activeTo == Approx(10.0f));
	CHECK(a.fadeHours == Approx(1.5f));
	CHECK(a.noiseAmount == Approx(0.6f));
	CHECK(a.noiseDetail == 2);

	CHECK(loaded[1].id == 9);
	CHECK(loaded[1].shape == FogVolumeShape::Ellipsoid);
}

TEST_CASE("Fog volume values are clamped", "[fog_volumes]")
{
	FogVolume volume;
	volume.size = Vector3(0.1f, 3.0f, -2.0f);
	volume.density = 5.0f;
	volume.color = Vector3(-1.0f, 3.0f, 1.0f);
	volume.edgeFade = 2.0f;
	volume.heightFalloff = -1.0f;
	volume.activeFrom = 25.0f;
	volume.activeTo = -2.0f;
	volume.fadeHours = 9.0f;
	volume.noiseAmount = 2.0f;
	volume.noiseDetail = 3;

	SanitizeFogVolume(volume);
	CHECK(volume.size.x == Approx(0.5f));
	CHECK(volume.size.z == Approx(0.5f));
	CHECK(volume.density == Approx(1.0f));
	CHECK(volume.color.x == Approx(0.0f));
	CHECK(volume.color.y == Approx(2.0f));
	CHECK(volume.edgeFade == Approx(1.0f));
	CHECK(volume.heightFalloff == Approx(0.0f));
	CHECK(volume.activeFrom == Approx(1.0f));
	CHECK(volume.activeTo == Approx(22.0f));
	CHECK(volume.fadeHours == Approx(6.0f));
	CHECK(volume.noiseAmount == Approx(1.0f));
	CHECK(volume.noiseDetail == 1);
}

TEST_CASE("An empty fog volume file reads back empty", "[fog_volumes]")
{
	std::vector<FogVolume> loaded;
	REQUIRE(readVolumes(writeVolumes({}), loaded));
	CHECK(loaded.empty());
}
