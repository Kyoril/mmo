// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_common/world_foliage.h"

#include "base/chunk_writer.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

#include <vector>

using namespace mmo;

namespace
{
	const ChunkMagic FoliageVersionChunk = MakeChunkMagic('FVER');
	const ChunkMagic FoliageMeshNamesChunk = MakeChunkMagic('FMSH');
	const ChunkMagic FoliageInstanceChunk = MakeChunkMagic('FINS');

	FoliageInstance MakeInstance(uint64 id, const char* mesh, float x, bool collides = true)
	{
		FoliageInstance instance;
		instance.uniqueId = id;
		instance.meshName = mesh;
		instance.position = Vector3(x, 2.0f, 3.0f);
		instance.rotation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
		instance.scale = Vector3(1.5f, 1.5f, 1.5f);
		instance.collides = collides;

		return instance;
	}

	/// Serializes with the production writer and reads the result back.
	std::vector<FoliageInstance> RoundTrip(const std::vector<FoliageInstance>& instances)
	{
		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		io::Writer writer(sink);
		WorldFoliageSerializer::Write(writer, instances);

		io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
		io::Reader reader(source);

		WorldFoliageLoader loader;
		REQUIRE(loader.Read(reader));

		return loader.GetInstances();
	}

	/// Hand-writes a .hfol whose FINS records carry no collision flag, i.e. exactly what an
	/// editor predating version 0x0002 produced.
	std::vector<char> WriteLegacyFile(uint32 version, const std::vector<FoliageInstance>& instances,
		const std::vector<String>& meshNames)
	{
		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		io::Writer writer(sink);

		{
			ChunkWriter versionChunk(FoliageVersionChunk, writer);
			writer << io::write<uint32>(version);
			versionChunk.Finish();
		}

		{
			ChunkWriter meshNamesChunk(FoliageMeshNamesChunk, writer);
			for (const auto& meshName : meshNames)
			{
				writer << io::write_dynamic_range<uint16>(meshName);
			}
			meshNamesChunk.Finish();
		}

		{
			ChunkWriter instanceChunk(FoliageInstanceChunk, writer);
			writer << io::write<uint32>(static_cast<uint32>(instances.size()));

			for (const auto& instance : instances)
			{
				writer
					<< io::write<uint64>(instance.uniqueId)
					<< io::write<uint32>(0)
					<< io::write<float>(instance.position.x)
					<< io::write<float>(instance.position.y)
					<< io::write<float>(instance.position.z)
					<< io::write<float>(instance.rotation.w)
					<< io::write<float>(instance.rotation.x)
					<< io::write<float>(instance.rotation.y)
					<< io::write<float>(instance.rotation.z)
					<< io::write<float>(instance.scale.x)
					<< io::write<float>(instance.scale.y)
					<< io::write<float>(instance.scale.z);
				// No collision byte -- that is the point of this fixture.
			}

			instanceChunk.Finish();
		}

		return buffer;
	}

	bool LoadFrom(const std::vector<char>& buffer, WorldFoliageLoader& loader)
	{
		io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
		io::Reader reader(source);

		return loader.Read(reader);
	}
}

TEST_CASE("WorldFoliage round-trips a single instance", "[game_common][foliage]")
{
	const auto loaded = RoundTrip({ MakeInstance(42, "Trees/Oak.hmsh", 10.0f) });

	REQUIRE(loaded.size() == 1);
	CHECK(loaded[0].uniqueId == 42);
	CHECK(loaded[0].meshName == "Trees/Oak.hmsh");
	CHECK(loaded[0].position.x == Approx(10.0f));
	CHECK(loaded[0].position.y == Approx(2.0f));
	CHECK(loaded[0].position.z == Approx(3.0f));
	CHECK(loaded[0].rotation.w == Approx(1.0f));
	CHECK(loaded[0].scale.x == Approx(1.5f));
	CHECK(loaded[0].collides);
}

TEST_CASE("WorldFoliage round-trips the collision flag in both states",
	"[game_common][foliage]")
{
	const auto loaded = RoundTrip({
		MakeInstance(1, "Trees/Oak.hmsh", 0.0f, true),
		MakeInstance(2, "Bushes/Fern.hmsh", 1.0f, false),
	});

	REQUIRE(loaded.size() == 2);
	CHECK(loaded[0].collides);
	CHECK_FALSE(loaded[1].collides);
}

TEST_CASE("WorldFoliage dedupes the mesh name table but keeps per-instance names",
	"[game_common][foliage]")
{
	const auto loaded = RoundTrip({
		MakeInstance(1, "Trees/Oak.hmsh", 0.0f),
		MakeInstance(2, "Trees/Oak.hmsh", 1.0f),
		MakeInstance(3, "Bushes/Fern.hmsh", 2.0f),
		MakeInstance(4, "Trees/Oak.hmsh", 3.0f),
	});

	REQUIRE(loaded.size() == 4);
	CHECK(loaded[0].meshName == "Trees/Oak.hmsh");
	CHECK(loaded[1].meshName == "Trees/Oak.hmsh");
	CHECK(loaded[2].meshName == "Bushes/Fern.hmsh");
	CHECK(loaded[3].meshName == "Trees/Oak.hmsh");
}

TEST_CASE("WorldFoliage dedupe actually shrinks the file", "[game_common][foliage]")
{
	// Four instances of one mesh must not cost four copies of the name. Guards the dedupe
	// loop against being simplified into a straight per-instance write.
	std::vector<char> deduped;
	{
		io::VectorSink sink(deduped);
		io::Writer writer(sink);
		WorldFoliageSerializer::Write(writer, {
			MakeInstance(1, "Trees/AVeryLongMeshNameIndeed.hmsh", 0.0f),
			MakeInstance(2, "Trees/AVeryLongMeshNameIndeed.hmsh", 1.0f),
			MakeInstance(3, "Trees/AVeryLongMeshNameIndeed.hmsh", 2.0f),
			MakeInstance(4, "Trees/AVeryLongMeshNameIndeed.hmsh", 3.0f),
		});
	}

	std::vector<char> distinct;
	{
		io::VectorSink sink(distinct);
		io::Writer writer(sink);
		WorldFoliageSerializer::Write(writer, {
			MakeInstance(1, "Trees/AVeryLongMeshNameIndeedA.hmsh", 0.0f),
			MakeInstance(2, "Trees/AVeryLongMeshNameIndeedB.hmsh", 1.0f),
			MakeInstance(3, "Trees/AVeryLongMeshNameIndeedC.hmsh", 2.0f),
			MakeInstance(4, "Trees/AVeryLongMeshNameIndeedD.hmsh", 3.0f),
		});
	}

	CHECK(deduped.size() < distinct.size());
}

TEST_CASE("WorldFoliage round-trips an empty instance list", "[game_common][foliage]")
{
	CHECK(RoundTrip({}).empty());
}

TEST_CASE("WorldFoliage defaults collides to true for version 0x0001 files",
	"[game_common][foliage]")
{
	// The version gate this test exists for. Instances written before the collision flag
	// existed have to keep colliding, or every tree in an old page silently becomes
	// walkable.
	const auto buffer = WriteLegacyFile(foliage_version::Version_0_0_0_1,
		{ MakeInstance(7, "Trees/Oak.hmsh", 5.0f, false) },
		{ "Trees/Oak.hmsh" });

	WorldFoliageLoader loader;
	REQUIRE(LoadFrom(buffer, loader));

	REQUIRE(loader.GetInstances().size() == 1);
	CHECK(loader.GetInstances()[0].uniqueId == 7);
	// The instance was built with collides == false, but a v1 file cannot express that, so
	// the loader must report true regardless.
	CHECK(loader.GetInstances()[0].collides);
}

TEST_CASE("WorldFoliage rejects a version below the oldest supported one",
	"[game_common][foliage]")
{
	const auto buffer = WriteLegacyFile(0, {}, { "Trees/Oak.hmsh" });

	WorldFoliageLoader loader;
	CHECK_FALSE(LoadFrom(buffer, loader));
}

TEST_CASE("WorldFoliage rejects an instance referencing an unknown mesh index",
	"[game_common][foliage]")
{
	// Mesh table is empty of the referenced index: the record points at entry 0 but no
	// names were written.
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		io::Writer writer(sink);

		{
			ChunkWriter versionChunk(FoliageVersionChunk, writer);
			writer << io::write<uint32>(foliage_version::Version_0_0_0_2);
			versionChunk.Finish();
		}
		{
			ChunkWriter meshNamesChunk(FoliageMeshNamesChunk, writer);
			writer << io::write_dynamic_range<uint16>(String("Trees/Oak.hmsh"));
			meshNamesChunk.Finish();
		}
		{
			ChunkWriter instanceChunk(FoliageInstanceChunk, writer);
			writer << io::write<uint32>(1);
			writer
				<< io::write<uint64>(1)
				<< io::write<uint32>(99)	// out of range
				<< io::write<float>(0.0f) << io::write<float>(0.0f) << io::write<float>(0.0f)
				<< io::write<float>(1.0f) << io::write<float>(0.0f)
				<< io::write<float>(0.0f) << io::write<float>(0.0f)
				<< io::write<float>(1.0f) << io::write<float>(1.0f) << io::write<float>(1.0f)
				<< io::write<uint8>(1);
			instanceChunk.Finish();
		}
	}

	WorldFoliageLoader loader;
	CHECK_FALSE(LoadFrom(buffer, loader));
}

TEST_CASE("WorldFoliage rejects instances arriving before the mesh name table",
	"[game_common][foliage]")
{
	// Instance records index into the mesh table, so the reader cannot resolve them if the
	// chunks are ordered the other way round. The record count has to be non-zero for this
	// to be a real conflict -- zero records need no names, which is the empty-page case.
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		io::Writer writer(sink);

		{
			ChunkWriter versionChunk(FoliageVersionChunk, writer);
			writer << io::write<uint32>(foliage_version::Version_0_0_0_2);
			versionChunk.Finish();
		}
		{
			ChunkWriter instanceChunk(FoliageInstanceChunk, writer);
			writer << io::write<uint32>(1);
			writer
				<< io::write<uint64>(1)
				<< io::write<uint32>(0)
				<< io::write<float>(0.0f) << io::write<float>(0.0f) << io::write<float>(0.0f)
				<< io::write<float>(1.0f) << io::write<float>(0.0f)
				<< io::write<float>(0.0f) << io::write<float>(0.0f)
				<< io::write<float>(1.0f) << io::write<float>(1.0f) << io::write<float>(1.0f)
				<< io::write<uint8>(1);
			instanceChunk.Finish();
		}
	}

	WorldFoliageLoader loader;
	CHECK_FALSE(LoadFrom(buffer, loader));
}

TEST_CASE("WorldFoliage TakeInstances empties the loader", "[game_common][foliage]")
{
	std::vector<char> buffer;
	{
		io::VectorSink sink(buffer);
		io::Writer writer(sink);
		WorldFoliageSerializer::Write(writer, { MakeInstance(1, "Trees/Oak.hmsh", 0.0f) });
	}

	WorldFoliageLoader loader;
	REQUIRE(LoadFrom(buffer, loader));

	const std::vector<FoliageInstance> taken = loader.TakeInstances();
	CHECK(taken.size() == 1);
	CHECK(loader.GetInstances().empty());
}
