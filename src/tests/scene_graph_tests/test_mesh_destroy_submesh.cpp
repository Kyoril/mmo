// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/mesh.h"

#include <string>

using namespace mmo;

namespace
{
	// Enough names that the unordered name map visits several of them after the removed one,
	// whatever its hash order is.
	constexpr uint16 subMeshCount = 40;

	String NameOf(const uint16 index)
	{
		return "Part_" + std::to_string(index);
	}

	void CreateNamedSubMeshes(Mesh& mesh)
	{
		for (uint16 i = 0; i < subMeshCount; ++i)
		{
			mesh.CreateSubMesh(NameOf(i));
		}
	}

	/// Every surviving name must resolve to the index of the sub mesh it was given to.
	void CheckNamesAfterRemoval(const Mesh& mesh, const uint16 removed)
	{
		REQUIRE(mesh.GetSubMeshCount() == subMeshCount - 1);
		CHECK(mesh.GetSubMeshIndex(NameOf(removed)) == -1);

		for (uint16 i = 0; i < subMeshCount; ++i)
		{
			if (i == removed)
			{
				continue;
			}

			const int32 expected = i < removed ? i : i - 1;
			INFO("sub mesh " << NameOf(i));
			CHECK(mesh.GetSubMeshIndex(NameOf(i)) == expected);

			String name;
			REQUIRE(mesh.GetSubMeshName(static_cast<uint16>(expected), name));
			CHECK(name == NameOf(i));
		}
	}
}

TEST_CASE("Destroying a sub mesh by index shifts every later sub mesh name", "[mesh]")
{
	Mesh mesh("DestroySubMeshByIndex");
	CreateNamedSubMeshes(mesh);

	mesh.DestroySubMesh(static_cast<uint16>(5));

	CheckNamesAfterRemoval(mesh, 5);
}

TEST_CASE("Destroying a sub mesh by name shifts every later sub mesh name", "[mesh]")
{
	Mesh mesh("DestroySubMeshByName");
	CreateNamedSubMeshes(mesh);

	mesh.DestroySubMesh(NameOf(5));

	CheckNamesAfterRemoval(mesh, 5);
}

TEST_CASE("Destroying the first and the last sub mesh keeps the other names", "[mesh]")
{
	Mesh first("DestroyFirstSubMesh");
	CreateNamedSubMeshes(first);
	first.DestroySubMesh(static_cast<uint16>(0));
	CheckNamesAfterRemoval(first, 0);

	Mesh last("DestroyLastSubMesh");
	CreateNamedSubMeshes(last);
	last.DestroySubMesh(static_cast<uint16>(subMeshCount - 1));
	CheckNamesAfterRemoval(last, subMeshCount - 1);
}
