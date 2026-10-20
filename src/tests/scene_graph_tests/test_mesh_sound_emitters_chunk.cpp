// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_serializer.h"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

using namespace mmo;

namespace
{
	std::vector<char> Serialize(const MeshPtr& mesh, const MeshVersion version = mesh_version::Latest)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		MeshSerializer serializer;
		serializer.Serialize(mesh, writer, version);
		return buffer;
	}

	MeshPtr Deserialize(const std::vector<char>& buffer, bool& ok)
	{
		auto mesh = std::make_shared<Mesh>("Loaded");
		io::MemorySource source{ buffer };
		io::Reader reader{ source };
		MeshDeserializer deserializer{ *mesh };
		ok = deserializer.Read(reader);
		return mesh;
	}

	void AppendChunk(std::vector<char>& buffer, const uint32 magic, const std::vector<uint8>& payload)
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		writer << io::write<uint32>(magic) << io::write<uint32>(payload.size());
		for (const uint8 byte : payload)
		{
			writer << io::write<uint8>(byte);
		}
	}

	bool ContainsMagic(const std::vector<char>& buffer, const char* magic)
	{
		const std::string_view bytes{ buffer.data(), buffer.size() };
		return bytes.find(std::string_view{ magic, 4 }) != std::string_view::npos;
	}
}

TEST_CASE("Mesh sound emitters survive a save and load", "[mesh_serializer][mesh_sounds]")
{
	auto mesh = std::make_shared<Mesh>("Torch");

	MeshSoundEmitter flame;
	flame.soundEntryId = 300;
	flame.offset = Vector3(0.0f, 1.25f, 0.1f);
	flame.volume = 0.6f;
	flame.maxDistance = 9.0f;

	MeshSoundEmitter hum;
	hum.soundEntryId = 301;

	mesh->SetSoundEmitters({ flame, hum });

	bool ok = false;
	const MeshPtr loaded = Deserialize(Serialize(mesh), ok);
	REQUIRE(ok);

	const auto& emitters = loaded->GetSoundEmitters();
	REQUIRE(emitters.size() == 2);
	CHECK(emitters[0].soundEntryId == 300);
	CHECK(emitters[0].offset.y == Approx(1.25f));
	CHECK(emitters[0].offset.z == Approx(0.1f));
	CHECK(emitters[0].volume == Approx(0.6f));
	CHECK(emitters[0].maxDistance == Approx(9.0f));
	CHECK(emitters[1].soundEntryId == 301);
	CHECK(emitters[1].volume == Approx(1.0f));
	CHECK(emitters[1].maxDistance == Approx(0.0f));
}

TEST_CASE("A mesh without sound emitters writes no SNDS chunk", "[mesh_serializer][mesh_sounds]")
{
	const auto mesh = std::make_shared<Mesh>("Silent");
	CHECK_FALSE(ContainsMagic(Serialize(mesh), "SNDS"));
}

TEST_CASE("Sound emitters are not written into pre-0x0302 meshes", "[mesh_serializer][mesh_sounds]")
{
	// Readers before 0x0302 fail on unknown chunks, so an old-format file must never carry one.
	auto mesh = std::make_shared<Mesh>("Old");
	MeshSoundEmitter emitter;
	emitter.soundEntryId = 5;
	mesh->SetSoundEmitters({ emitter });

	CHECK_FALSE(ContainsMagic(Serialize(mesh, mesh_version::Version_0_3_1), "SNDS"));
}

TEST_CASE("A sound emitter chunk with an unknown layout is skipped, not fatal", "[mesh_serializer][mesh_sounds]")
{
	std::vector<char> buffer = Serialize(std::make_shared<Mesh>("Future"));

	// Layout 99, count 1, then bytes this build cannot interpret.
	AppendChunk(buffer, 'SNDS', { 99, 1, 0, 1, 2, 3, 4, 5, 6, 7 });

	bool ok = false;
	const MeshPtr loaded = Deserialize(buffer, ok);
	CHECK(ok);
	CHECK(loaded->GetSoundEmitters().empty());
}

TEST_CASE("A truncated sound emitter chunk fails the load", "[mesh_serializer][mesh_sounds]")
{
	std::vector<char> buffer = Serialize(std::make_shared<Mesh>("Broken"));

	// Layout 1 claiming two emitters, but only a few bytes follow.
	AppendChunk(buffer, 'SNDS', { 1, 2, 0, 1, 2, 3 });

	bool ok = true;
	Deserialize(buffer, ok);
	CHECK_FALSE(ok);
}
