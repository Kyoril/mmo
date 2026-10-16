// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_common/world_entity_loader.h"

#include "base/chunk_reader.h"
#include "base/chunk_writer.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

#include <vector>

using namespace mmo;

namespace
{
	const ChunkMagic VersionChunk = MakeChunkMagic('WVER');
	const ChunkMagic MeshChunk = MakeChunkMagic('WMSH');
	const ChunkMagic WorldModelChunk = MakeChunkMagic('WWMO');

	/// Writes the entity payload exactly like WorldEditorInstance::Save does.
	void WriteEntityPayload(io::Writer& writer, const String& asset, bool worldModel)
	{
		writer
			<< io::write<uint64>(42)
			<< io::write_dynamic_range<uint16>(asset)
			<< io::write<float>(1.0f) << io::write<float>(2.0f) << io::write<float>(3.0f)
			<< io::write<float>(1.0f) << io::write<float>(0.0f) << io::write<float>(0.0f) << io::write<float>(0.0f)
			<< io::write<float>(1.0f) << io::write<float>(1.0f) << io::write<float>(1.0f);

		if (!worldModel)
		{
			writer << io::write<uint8>(0);
		}

		writer
			<< io::write_dynamic_range<uint8>(String("Inn"))
			<< io::write_dynamic_range<uint16>(String("Buildings"));
	}

	/// Builds a version 3 placement file; kits are appended through the production writer helper.
	std::vector<char> BuildFile(bool worldModel, const std::vector<String>& kits)
	{
		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		io::Writer writer(sink);

		{
			ChunkWriter chunkWriter(VersionChunk, writer);
			writer << io::write<uint32>(3);
			chunkWriter.Finish();
		}

		{
			ChunkWriter chunkWriter(worldModel ? WorldModelChunk : MeshChunk, writer);
			WriteEntityPayload(writer, worldModel ? "Models/Inn.hwmo" : "Models/Crate.hmsh", worldModel);
			chunkWriter.Finish();
		}

		WriteWorldModelKitChunk(writer, kits);
		return buffer;
	}

	bool Load(const std::vector<char>& buffer, WorldEntityLoader& loader)
	{
		io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
		io::Reader reader(source);
		return loader.Read(reader);
	}

	/// A reader that knows nothing about kits: only the version chunk and the world model chunk.
	class PlainReader final : public ChunkReader
	{
	public:
		PlainReader()
			: ChunkReader(true)
		{
			AddChunkHandler(*VersionChunk, true, *this, &PlainReader::OnVersion);
			AddChunkHandler(*WorldModelChunk, true, *this, &PlainReader::OnWorldModel);
		}

		uint64 uniqueId = 0;

	private:
		bool OnVersion(io::Reader& reader, uint32, uint32)
		{
			uint32 version = 0;
			return reader >> io::read<uint32>(version);
		}

		bool OnWorldModel(io::Reader& reader, uint32, uint32)
		{
			return reader >> io::read<uint64>(uniqueId);
		}
	};
}

TEST_CASE("World model placement without a kit chunk has no kits", "[game_common][world_entity]")
{
	const auto buffer = BuildFile(true, {});

	WorldEntityLoader loader;
	REQUIRE(Load(buffer, loader));

	const auto& entity = loader.GetEntity();
	CHECK(entity.entityType == WorldEntityType::WorldModel);
	CHECK(entity.meshName == "Models/Inn.hwmo");
	CHECK(entity.position.x == 1.0f);
	CHECK(entity.position.y == 2.0f);
	CHECK(entity.position.z == 3.0f);
	CHECK(entity.kitPaths.empty());
}

TEST_CASE("World model placement reads its kit list in order", "[game_common][world_entity]")
{
	const auto buffer = BuildFile(true, { "Kits/Inn_Base.hkit", "Kits/Harvest.hkit" });

	WorldEntityLoader loader;
	REQUIRE(Load(buffer, loader));

	const auto& entity = loader.GetEntity();
	CHECK(entity.meshName == "Models/Inn.hwmo");
	CHECK(entity.name == "Inn");
	REQUIRE(entity.kitPaths.size() == 2);
	CHECK(entity.kitPaths[0] == "Kits/Inn_Base.hkit");
	CHECK(entity.kitPaths[1] == "Kits/Harvest.hkit");
}

TEST_CASE("A reader without the kit handler still loads the building", "[game_common][world_entity]")
{
	const auto buffer = BuildFile(true, { "Kits/Inn_Base.hkit" });

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	PlainReader plain;
	REQUIRE(plain.Read(reader));
	CHECK(plain.uniqueId == 42);
}

TEST_CASE("Mesh placement ignores a kit chunk", "[game_common][world_entity]")
{
	const auto buffer = BuildFile(false, { "Kits/Inn_Base.hkit" });

	WorldEntityLoader loader;
	REQUIRE(Load(buffer, loader));

	const auto& entity = loader.GetEntity();
	CHECK(entity.entityType == WorldEntityType::Mesh);
	CHECK(entity.meshName == "Models/Crate.hmsh");
	CHECK(entity.kitPaths.empty());
}
