// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"
#include "math/collision_recipe.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_serializer.h"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

using namespace mmo;

namespace
{
	MeshPtr MakeMeshWithCollision()
	{
		auto mesh = std::make_shared<Mesh>("CollisionRecipeChunkTest");
		const std::vector<Vector3> vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } };
		const std::vector<uint32> indices = { 0, 2, 1, 0, 3, 2 };
		const std::vector<uint16> faceSubMeshes = { 0, 0 };
		mesh->GetCollisionTree().Build(vertices, indices, faceSubMeshes);
		return mesh;
	}

	CollisionRecipe MakeRecipe()
	{
		CollisionRecipe recipe;
		recipe.includedSubMeshes = { 0 };
		CollisionShape box;
		box.name = "Blocker";
		recipe.shapes.push_back(box);
		return recipe;
	}

	std::vector<char> Serialize(const MeshPtr& mesh, MeshVersion version, const CollisionRecipe* recipe)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		MeshSerializer serializer;
		serializer.Serialize(mesh, writer, version, recipe);
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

	void AppendChunk(std::vector<char>& buffer, const uint32 magic, const std::vector<char>& payload)
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		writer << io::write<uint32>(magic) << io::write<uint32>(payload.size());
		for (const char byte : payload)
		{
			writer << io::write<uint8>(static_cast<uint8>(byte));
		}
	}

	collision_recipe_read::Type ReadRecipe(const std::vector<char>& buffer, CollisionRecipe& out_recipe)
	{
		io::MemorySource source{ buffer };
		io::Reader reader{ source };
		return ReadMeshCollisionRecipe(reader, out_recipe);
	}
}

TEST_CASE("Runtime loads a 0x0302 mesh with a recipe chunk and an identical tree", "[mesh_serializer]")
{
	const MeshPtr mesh = MakeMeshWithCollision();
	const CollisionRecipe recipe = MakeRecipe();
	const std::vector<char> buffer = Serialize(mesh, mesh_version::Latest, &recipe);

	bool ok = false;
	const MeshPtr loaded = Deserialize(buffer, ok);
	REQUIRE(ok);
	CHECK(loaded->GetCollisionTree().GetIndices() == mesh->GetCollisionTree().GetIndices());
	CHECK(loaded->GetCollisionTree().GetVertices().size() == mesh->GetCollisionTree().GetVertices().size());

	CollisionRecipe read;
	REQUIRE(ReadRecipe(buffer, read) == collision_recipe_read::Read);
	REQUIRE(read.shapes.size() == 1);
	CHECK(read.shapes[0].name == "Blocker");
}

TEST_CASE("Mesh without a recipe reports the recipe as absent", "[mesh_serializer]")
{
	const std::vector<char> buffer = Serialize(MakeMeshWithCollision(), mesh_version::Latest, nullptr);
	CollisionRecipe read;
	CHECK(ReadRecipe(buffer, read) == collision_recipe_read::Absent);
}

TEST_CASE("A 0x0301 mesh still loads and never carries a recipe", "[mesh_serializer]")
{
	const CollisionRecipe recipe = MakeRecipe();
	const std::vector<char> buffer = Serialize(MakeMeshWithCollision(), mesh_version::Version_0_3_1, &recipe);

	bool ok = false;
	const MeshPtr loaded = Deserialize(buffer, ok);
	REQUIRE(ok);
	CHECK_FALSE(loaded->GetCollisionTree().IsEmpty());

	CollisionRecipe read;
	CHECK(ReadRecipe(buffer, read) == collision_recipe_read::Absent);
}

TEST_CASE("A 0x0302 mesh with an unknown extra chunk still loads", "[mesh_serializer]")
{
	std::vector<char> buffer = Serialize(MakeMeshWithCollision(), mesh_version::Latest, nullptr);
	AppendChunk(buffer, 'XTRA', { 1, 2, 3, 4, 5 });

	bool ok = false;
	const MeshPtr loaded = Deserialize(buffer, ok);
	CHECK(ok);
	CHECK_FALSE(loaded->GetCollisionTree().IsEmpty());
}

TEST_CASE("A corrupt recipe chunk is reported and does not break the runtime load", "[mesh_serializer]")
{
	std::vector<char> buffer = Serialize(MakeMeshWithCollision(), mesh_version::Latest, nullptr);
	AppendChunk(buffer, 'CSRC', { 9, 9 });    // too short for even the version field

	bool ok = false;
	const MeshPtr loaded = Deserialize(buffer, ok);
	CHECK(ok);
	CHECK_FALSE(loaded->GetCollisionTree().IsEmpty());

	CollisionRecipe read;
	CHECK(ReadRecipe(buffer, read) == collision_recipe_read::Corrupt);
}

TEST_CASE("A recipe chunk with a valid version and a garbage count is reported as corrupt", "[mesh_serializer]")
{
	std::vector<char> buffer = Serialize(MakeMeshWithCollision(), mesh_version::Latest, nullptr);

	// Version 1, useRender, then an included-submesh count of 0xFFFFFFFF and nothing else.
	AppendChunk(buffer, 'CSRC', { 1, 0, 0, 0, 1, char(0xFF), char(0xFF), char(0xFF), char(0xFF) });

	bool ok = false;
	const MeshPtr loaded = Deserialize(buffer, ok);
	CHECK(ok);

	CollisionRecipe read;
	CHECK(ReadRecipe(buffer, read) == collision_recipe_read::Corrupt);
}

TEST_CASE("A recipe chunk claiming more bytes than the file has is reported as corrupt", "[mesh_serializer]")
{
	std::vector<char> buffer = Serialize(MakeMeshWithCollision(), mesh_version::Latest, nullptr);
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		writer << io::write<uint32>('CSRC') << io::write<uint32>(0x7FFFFFFFu) << io::write<uint32>(CollisionRecipeVersion);
	}

	CollisionRecipe read;
	CHECK(ReadRecipe(buffer, read) == collision_recipe_read::Corrupt);
}
