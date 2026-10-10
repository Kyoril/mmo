// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/typedefs.h"
#include "math/collision_recipe.h"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

using namespace mmo;

namespace
{
	CollisionRecipe MakeRecipe()
	{
		CollisionRecipe recipe;
		recipe.useRenderGeometry = true;
		recipe.includedSubMeshes = { 0, 2 };

		CollisionShape helix;
		helix.type = collision_shape_type::HelixRamp;
		helix.op = collision_shape_op::Add;
		helix.name = "Spiral";
		helix.position = Vector3(1.0f, 2.0f, 3.0f);
		helix.rotation = Quaternion(Degree(45.0f), Vector3::UnitY);
		helix.scale = Vector3(4.0f, 3.0f, 4.0f);
		helix.surfaceSubMesh = 2;
		helix.segments = 48;
		helix.innerRadius = 0.3f;
		helix.sweepDegrees = 540.0f;
		helix.thickness = 0.1f;
		helix.clockwise = true;

		CollisionShape cut;
		cut.type = collision_shape_type::Cylinder;
		cut.op = collision_shape_op::Cut;
		cut.name = "Steps";

		CollisionShape plane;
		plane.type = collision_shape_type::Plane;
		plane.twoSided = true;

		recipe.shapes = { helix, cut, plane };
		return recipe;
	}

	std::vector<char> Write(const CollisionRecipe& recipe)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		writer << recipe;
		return buffer;
	}

	bool Read(const std::vector<char>& buffer, CollisionRecipe& out_recipe)
	{
		io::MemorySource source{ buffer };
		io::Reader reader{ source };
		reader >> out_recipe;
		return static_cast<bool>(reader);
	}
}

TEST_CASE("Collision recipe survives a serialization round trip", "[collision_recipe]")
{
	const CollisionRecipe recipe = MakeRecipe();

	CollisionRecipe loaded;
	REQUIRE(Read(Write(recipe), loaded));

	CHECK(loaded.useRenderGeometry);
	CHECK(loaded.includedSubMeshes == std::vector<uint16>{ 0, 2 });
	REQUIRE(loaded.shapes.size() == 3);

	const CollisionShape& h = loaded.shapes[0];
	CHECK(h.type == collision_shape_type::HelixRamp);
	CHECK(h.name == "Spiral");
	CHECK(h.position.y == Approx(2.0f));
	CHECK(h.rotation.w == Approx(recipe.shapes[0].rotation.w));
	CHECK(h.rotation.y == Approx(recipe.shapes[0].rotation.y));
	CHECK(h.scale.x == Approx(4.0f));
	CHECK(h.surfaceSubMesh == 2);
	CHECK(h.segments == 48);
	CHECK(h.innerRadius == Approx(0.3f));
	CHECK(h.sweepDegrees == Approx(540.0f));
	CHECK(h.thickness == Approx(0.1f));
	CHECK(h.clockwise);

	CHECK(loaded.shapes[1].op == collision_shape_op::Cut);
	CHECK(loaded.shapes[1].name == "Steps");
	CHECK(loaded.shapes[2].twoSided);
}

TEST_CASE("Collision recipe with an unknown version fails to read", "[collision_recipe]")
{
	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << io::write<uint32>(CollisionRecipeVersion + 1);

	CollisionRecipe loaded;
	CHECK_FALSE(Read(buffer, loaded));
}

TEST_CASE("Collision recipe with an invalid shape type fails to read", "[collision_recipe]")
{
	std::vector<char> buffer = Write(MakeRecipe());

	// Shape type is the first byte after: version(4) + useRender(1) + count(4) + 2 ids(4) + shape count(4).
	buffer[4 + 1 + 4 + 4 + 4] = static_cast<char>(collision_shape_type::Count_);

	CollisionRecipe loaded;
	CHECK_FALSE(Read(buffer, loaded));
}

TEST_CASE("Truncated collision recipe fails to read", "[collision_recipe]")
{
	std::vector<char> buffer = Write(MakeRecipe());
	buffer.resize(buffer.size() / 2);

	CollisionRecipe loaded;
	CHECK_FALSE(Read(buffer, loaded));
}

TEST_CASE("Included submeshes are inferred from a tree's face submesh ids", "[collision_recipe]")
{
	CHECK(InferIncludedSubMeshes({ 3, 0, 3, 3, 1, 0 }) == std::vector<uint16>{ 0, 1, 3 });
	CHECK(InferIncludedSubMeshes({}).empty());
}

TEST_CASE("Sanitizing a recipe drops submesh ids the mesh no longer has", "[collision_recipe]")
{
	CollisionRecipe recipe = MakeRecipe();
	recipe.includedSubMeshes = { 0, 2, 5, 2 };
	recipe.shapes[0].surfaceSubMesh = 9;
	recipe.shapes[2].op = collision_shape_op::Cut;

	SanitizeCollisionRecipe(recipe, 3);

	CHECK(recipe.includedSubMeshes == std::vector<uint16>{ 0, 2 });
	CHECK(recipe.shapes[0].surfaceSubMesh == 0);
	CHECK(recipe.shapes[2].op == collision_shape_op::Add);

	SanitizeCollisionRecipe(recipe, 0);
	CHECK(recipe.includedSubMeshes.empty());
	CHECK(recipe.shapes[0].surfaceSubMesh == 0);
}
