// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_common/fog_volume.h"
#include "game_common/world_fog_volumes.h"

#include "base/chunk_writer.h"
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

	// Mirrors the private chunk magics in world_fog_volumes.cpp.
	const ChunkMagic VersionChunkMagic = MakeChunkMagic('FGVR');

	std::vector<char> writeVersionChunk(const uint32 version)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		ChunkWriter versionChunk(VersionChunkMagic, writer);
		writer << io::write<uint32>(version);
		versionChunk.Finish();
		return buffer;
	}

	// An unused four-character magic that the deserializer never registers a handler for.
	const ChunkMagic UnknownChunkMagic = MakeChunkMagic('ZZZZ');

	std::vector<char> writeUnknownChunk()
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		ChunkWriter unknownChunk(UnknownChunkMagic, writer);
		writer << io::write<uint32>(0xDEADBEEFu) << io::write<uint8>(1) << io::write<uint8>(2) << io::write<uint8>(3);
		unknownChunk.Finish();
		return buffer;
	}

	// Mirrors the private volume-data chunk magic and record layout in world_fog_volumes.cpp, so a
	// test can assemble a standalone FGVL chunk with other chunks spliced around it.
	const ChunkMagic VolumeDataChunkMagic = MakeChunkMagic('FGVL');

	std::vector<char> writeVolumeDataChunk(const std::vector<FogVolume>& volumes)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };

		ChunkWriter volumesChunk(VolumeDataChunkMagic, writer);
		writer << io::write<uint32>(static_cast<uint32>(volumes.size()));

		for (const auto& volume : volumes)
		{
			writer
				<< io::write<uint32>(volume.id)
				<< io::write_dynamic_range<uint16>(volume.name)
				<< io::write<uint8>(static_cast<uint8>(volume.shape))
				<< io::write<float>(volume.position.x)
				<< io::write<float>(volume.position.y)
				<< io::write<float>(volume.position.z)
				<< io::write<float>(volume.size.x)
				<< io::write<float>(volume.size.y)
				<< io::write<float>(volume.size.z)
				<< io::write<float>(volume.yaw)
				<< io::write<float>(volume.density)
				<< io::write<float>(volume.color.x)
				<< io::write<float>(volume.color.y)
				<< io::write<float>(volume.color.z)
				<< io::write<float>(volume.edgeFade)
				<< io::write<float>(volume.heightFalloff)
				<< io::write<float>(volume.activeFrom)
				<< io::write<float>(volume.activeTo)
				<< io::write<float>(volume.fadeHours)
				<< io::write<float>(volume.noiseAmount)
				<< io::write<uint8>(volume.noiseDetail);
		}

		volumesChunk.Finish();
		return buffer;
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

TEST_CASE("A fog volume file truncated mid-record reads back false without crashing", "[fog_volumes]")
{
	FogVolume first;
	first.id = 1;
	first.name = "First";

	FogVolume second;
	second.id = 2;
	second.name = "Second";

	std::vector<char> buffer = writeVolumes({ first, second });

	// Cut the buffer off partway through the second volume's fixed-size fields (well after the
	// count and the first, fully-written volume). The declared chunk size in the header still
	// claims the full, untruncated payload, so the deserializer has to notice the source ran out
	// of data rather than reading past the end of the buffer.
	REQUIRE(buffer.size() > 10);
	buffer.resize(buffer.size() - 10);

	std::vector<FogVolume> loaded;
	CHECK_FALSE(readVolumes(buffer, loaded));
}

TEST_CASE("A fog volume version newer than supported is rejected", "[fog_volumes]")
{
	const std::vector<char> buffer = writeVersionChunk(2);

	std::vector<FogVolume> loaded;
	CHECK_FALSE(readVolumes(buffer, loaded));
	CHECK(loaded.empty());
}

TEST_CASE("An unknown chunk between the version and volume chunks is skipped", "[fog_volumes]")
{
	FogVolume volume;
	volume.id = 42;
	volume.name = "Skipped-chunk survivor";

	std::vector<char> buffer = writeVersionChunk(fog_volume_version::Version_0_0_0_1);
	const std::vector<char> unknownChunk = writeUnknownChunk();
	buffer.insert(buffer.end(), unknownChunk.begin(), unknownChunk.end());
	const std::vector<char> volumeChunk = writeVolumeDataChunk({ volume });
	buffer.insert(buffer.end(), volumeChunk.begin(), volumeChunk.end());

	std::vector<FogVolume> loaded;
	REQUIRE(readVolumes(buffer, loaded));
	REQUIRE(loaded.size() == 1);
	CHECK(loaded[0].id == 42);
	CHECK(loaded[0].name == "Skipped-chunk survivor");
}
