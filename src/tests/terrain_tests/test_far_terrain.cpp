// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "terrain/far_terrain_mesh.h"
#include "terrain/far_terrain_selection.h"

#include <algorithm>
#include <vector>

using namespace mmo;
using namespace mmo::terrain;

namespace
{
	Vector3 FaceNormal(const std::vector<far_mesh::Vertex>& vertices, const uint16 a, const uint16 b, const uint16 c)
	{
		const Vector3& pa = vertices[a].position;
		return (vertices[b].position - pa).Cross(vertices[c].position - pa);
	}

	void MakeFlatLod(const float height, std::vector<float>& heights, std::vector<EncodedNormal8>& normals)
	{
		heights.assign(far_mesh::GridVertexCount, height);
		normals.assign(far_mesh::GridVertexCount, EncodeNormalSNorm8(0.0f, 1.0f, 0.0f));
	}

	bool Contains(const std::vector<far_selection::PageCoord>& pages, const uint32 x, const uint32 z)
	{
		return std::find(pages.begin(), pages.end(), far_selection::PageCoord{ x, z }) != pages.end();
	}
}

TEST_CASE("Far page grid spans exactly one page", "[far_terrain]")
{
	std::vector<float> heights;
	std::vector<EncodedNormal8> normals;
	MakeFlatLod(12.0f, heights, normals);

	std::vector<far_mesh::Vertex> vertices;
	far_mesh::BuildVertices(heights.data(), normals.data(), 25.0f, vertices);

	REQUIRE(vertices.size() == far_mesh::VertexCount);
	CHECK(vertices.front().position.x == 0.0f);
	CHECK(vertices.front().position.z == 0.0f);

	const far_mesh::Vertex& last = vertices[far_mesh::GridVertexCount - 1];
	CHECK(last.position.x == Approx(constants::PageSize));
	CHECK(last.position.z == Approx(constants::PageSize));
	CHECK(last.u == 1.0f);
	CHECK(last.v == 1.0f);
	CHECK(last.position.y == 12.0f);
}

TEST_CASE("Far page skirt hangs below every border vertex", "[far_terrain]")
{
	std::vector<float> heights;
	std::vector<EncodedNormal8> normals;
	MakeFlatLod(0.0f, heights, normals);
	for (uint32 i = 0; i < far_mesh::GridVertexCount; ++i)
	{
		heights[i] = static_cast<float>(i);
	}

	std::vector<far_mesh::Vertex> vertices;
	far_mesh::BuildVertices(heights.data(), normals.data(), 25.0f, vertices);

	// Every border vertex appears exactly once in the perimeter walk.
	std::vector<int> seen(far_mesh::GridVertexCount, 0);
	for (uint32 k = 0; k < far_mesh::PerimeterVertexCount; ++k)
	{
		uint32 i, j;
		far_mesh::PerimeterCoord(k, i, j);
		REQUIRE((i == 0 || j == 0 || i == far_mesh::CellsPerSide || j == far_mesh::CellsPerSide));
		++seen[i + j * far_mesh::VerticesPerSide];

		const far_mesh::Vertex& border = vertices[i + j * far_mesh::VerticesPerSide];
		const far_mesh::Vertex& skirt = vertices[far_mesh::GridVertexCount + k];
		CHECK(skirt.position.x == border.position.x);
		CHECK(skirt.position.z == border.position.z);
		CHECK(skirt.position.y == border.position.y - 25.0f);
	}

	uint32 borderVertices = 0;
	for (const int count : seen)
	{
		CHECK(count <= 1);
		borderVertices += count;
	}
	CHECK(borderVertices == far_mesh::PerimeterVertexCount);
}

TEST_CASE("Far page triangles face up and the skirt faces outward", "[far_terrain]")
{
	std::vector<float> heights;
	std::vector<EncodedNormal8> normals;
	MakeFlatLod(0.0f, heights, normals);

	std::vector<far_mesh::Vertex> vertices;
	far_mesh::BuildVertices(heights.data(), normals.data(), 25.0f, vertices);

	std::vector<uint16> indices;
	far_mesh::BuildIndices(indices);
	REQUIRE(indices.size() == far_mesh::IndexCount);

	for (const uint16 index : indices)
	{
		REQUIRE(index < far_mesh::VertexCount);
	}

	const size_t gridIndexCount = far_mesh::CellsPerSide * far_mesh::CellsPerSide * 6;
	for (size_t t = 0; t < gridIndexCount; t += 3)
	{
		CHECK(FaceNormal(vertices, indices[t], indices[t + 1], indices[t + 2]).y > 0.0f);
	}

	const Vector3 pageCenter(static_cast<float>(constants::PageSize) * 0.5f, 0.0f, static_cast<float>(constants::PageSize) * 0.5f);
	for (size_t t = gridIndexCount; t < indices.size(); t += 3)
	{
		const Vector3 n = FaceNormal(vertices, indices[t], indices[t + 1], indices[t + 2]);
		Vector3 outward = vertices[indices[t]].position - pageCenter;
		outward.y = 0.0f;
		CHECK(n.Dot(outward) > 0.0f);
		CHECK(std::abs(n.y) < 1.0e-3f);
	}
}

TEST_CASE("Far terrain selects a round area around the viewer", "[far_terrain]")
{
	// Viewer in the centre of page (40, 20).
	const float pageSize = static_cast<float>(constants::PageSize);
	const float worldX = (40.0f - 32.0f + 0.5f) * pageSize;
	const float worldZ = (20.0f - 32.0f + 0.5f) * pageSize;

	std::vector<far_selection::PageCoord> pages;
	far_selection::SelectPages(worldX, worldZ, 4, pages);

	CHECK(Contains(pages, 40, 20));
	CHECK(Contains(pages, 44, 20));
	CHECK(Contains(pages, 40, 16));
	CHECK(Contains(pages, 43, 23));
	CHECK_FALSE(Contains(pages, 44, 24));
	CHECK_FALSE(Contains(pages, 45, 20));

	// The 9x9 square minus the far corners.
	CHECK(pages.size() > 49);
	CHECK(pages.size() < 81);
}

TEST_CASE("Far terrain selection stays inside the map", "[far_terrain]")
{
	std::vector<far_selection::PageCoord> pages;
	const float edge = -32.0f * static_cast<float>(constants::PageSize) + 10.0f;
	far_selection::SelectPages(edge, edge, 4, pages);

	REQUIRE_FALSE(pages.empty());
	for (const auto& page : pages)
	{
		CHECK(page.x < constants::MaxPages);
		CHECK(page.z < constants::MaxPages);
	}
	CHECK(Contains(pages, 0, 0));

	far_selection::SelectPages(0.0f, 0.0f, 0, pages);
	CHECK(pages.empty());
}
