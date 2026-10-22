// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"
#include "graphics/graphics_device.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "math/degree.h"
#include "math/quaternion.h"
#include "math/radian.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/mesh_merge.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/sub_mesh.h"

#include <cmath>
#include <vector>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	const String QuadMaterial = "test/mesh_merge/Quad.hmat";
	const String OverrideMaterial = "test/mesh_merge/Override.hmat";

	/// The unit quad in the XZ plane, facing +Y, as two triangles.
	std::vector<WorldModelMergeVertex> QuadVertices()
	{
		std::vector<WorldModelMergeVertex> v(4);
		v[0].pos = Vector3(0.0f, 0.0f, 0.0f);
		v[1].pos = Vector3(1.0f, 0.0f, 0.0f);
		v[2].pos = Vector3(1.0f, 0.0f, 1.0f);
		v[3].pos = Vector3(0.0f, 0.0f, 1.0f);
		for (auto& vertex : v)
		{
			vertex.normal = Vector3::UnitY;
			vertex.tangent = Vector3::UnitX;
			vertex.binormal = Vector3::UnitZ;
			vertex.color = 0xFFFFFFFF;
		}
		return v;
	}

	const std::vector<uint16> QuadIndices{ 0, 2, 1, 0, 3, 2 };

	/// Adds one submesh holding the quad, in the static mesh layout, to a mesh.
	void AddQuadSubMesh(Mesh& mesh, const String& material)
	{
		GraphicsDevice& device = EnsureNullDevice();

		SubMesh& subMesh = mesh.CreateSubMesh();
		subMesh.useSharedVertices = false;
		subMesh.SetMaterial(MaterialManager::Get().CreateManual(material));

		const std::vector<WorldModelMergeVertex> vertices = QuadVertices();
		subMesh.vertexData = std::make_unique<VertexData>(&device);
		subMesh.vertexData->vertexCount = static_cast<uint32>(vertices.size());
		subMesh.vertexData->vertexStart = 0;

		uint32 offset = 0;
		auto* decl = subMesh.vertexData->vertexDeclaration;
		offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
		offset += decl->AddElement(0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
		offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
		offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
		offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
		decl->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate);
		subMesh.vertexData->vertexBufferBinding->SetBinding(0, device.CreateVertexBuffer(
			static_cast<uint32>(vertices.size()), sizeof(WorldModelMergeVertex), BufferUsage::StaticWriteOnly, vertices.data()));

		subMesh.indexData = std::make_unique<IndexData>();
		subMesh.indexData->indexStart = 0;
		subMesh.indexData->indexCount = static_cast<uint32>(QuadIndices.size());
		subMesh.indexData->indexBuffer = device.CreateIndexBuffer(
			static_cast<uint32>(QuadIndices.size()), IndexBufferSize::Index_16, BufferUsage::StaticWriteOnly, QuadIndices.data());
	}

	/// A quad mesh with one submesh (or two, both the quad, with different materials).
	MeshPtr QuadMesh(const String& name, const bool twoSubMeshes = false)
	{
		const MeshPtr mesh = MeshManager::Get().CreateManual(name);
		if (mesh->GetSubMeshCount() == 0)
		{
			AddQuadSubMesh(*mesh, QuadMaterial);
			if (twoSubMeshes)
			{
				AddQuadSubMesh(*mesh, OverrideMaterial);
			}
			mesh->SetBounds(AABB(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 1.0f)));
		}
		return mesh;
	}

	void CheckVector(const Vector3& actual, const Vector3& expected)
	{
		CHECK(actual.x == Approx(expected.x).margin(1e-5f));
		CHECK(actual.y == Approx(expected.y).margin(1e-5f));
		CHECK(actual.z == Approx(expected.z).margin(1e-5f));
	}

	/// Face normal of a triangle from its winding (counter-clockwise front faces).
	Vector3 FaceNormal(const std::vector<WorldModelMergeVertex>& vertices, const std::vector<uint32>& indices, const size_t triangle)
	{
		const Vector3& a = vertices[indices[triangle * 3 + 0]].pos;
		const Vector3& b = vertices[indices[triangle * 3 + 1]].pos;
		const Vector3& c = vertices[indices[triangle * 3 + 2]].pos;
		return (b - a).Cross(c - a).NormalizedCopy();
	}
}

TEST_CASE("Sub mesh geometry is read back from its buffers", "[mesh_merge]")
{
	const MeshPtr mesh = QuadMesh("test/mesh_merge/ReadBack.hmsh");
	REQUIRE(CanMergeMesh(*mesh));

	MeshMergeGeometry geometry;
	REQUIRE(ReadSubMeshGeometry(mesh->GetSubMesh(0), geometry));

	REQUIRE(geometry.vertices.size() == 4);
	CheckVector(geometry.vertices[2].pos, Vector3(1.0f, 0.0f, 1.0f));
	CHECK(geometry.indices == std::vector<uint32>{ 0, 2, 1, 0, 3, 2 });
}

TEST_CASE("A mesh with a skeleton or without submeshes cannot be merged", "[mesh_merge]")
{
	const MeshPtr empty = MeshManager::Get().CreateManual("test/mesh_merge/Empty.hmsh");
	CHECK_FALSE(CanMergeMesh(*empty));

	const MeshPtr skinned = QuadMesh("test/mesh_merge/Skinned.hmsh");
	skinned->SetSkeletonName("test/mesh_merge/Skinned.skel");
	CHECK_FALSE(CanMergeMesh(*skinned));
}

TEST_CASE("Two quads with the same material merge into one submesh", "[mesh_merge]")
{
	const MeshPtr quad = QuadMesh("test/mesh_merge/Quad.hmsh");

	Matrix4 first;
	first.MakeTransform(Vector3(10.0f, 2.0f, -3.0f), Vector3(2.0f, 2.0f, 2.0f), Quaternion::Identity);
	Matrix4 second;
	second.MakeTransform(Vector3(-5.0f, 0.0f, 7.0f), Vector3(1.0f, 1.0f, 1.0f), Quaternion(Radian(Degree(90.0f)), Vector3::UnitY));

	MeshReadBackCache cache;
	MeshMergeAccumulator accumulator(cache);
	CHECK(accumulator.Add(*quad, first));
	CHECK(accumulator.Add(*quad, second));

	REQUIRE(accumulator.GetParts().size() == 1);
	const MeshMergeBuckets& buckets = accumulator.GetParts()[0];
	REQUIRE(buckets.size() == 1);
	const MeshMergeBucket& bucket = buckets.begin()->second;
	CHECK(buckets.begin()->first == QuadMaterial);
	REQUIRE(bucket.vertices.size() == 8);
	REQUIRE(bucket.indices.size() == 12);

	const std::vector<WorldModelMergeVertex> source = QuadVertices();
	for (size_t i = 0; i < 4; ++i)
	{
		CheckVector(bucket.vertices[i].pos, first * source[i].pos);
		CheckVector(bucket.vertices[4 + i].pos, second * source[i].pos);
	}
	CHECK(bucket.indices[6] == 4);

	// The merged mesh holds them as one submesh, named so its name is never empty
	MeshMergeBuckets copy = buckets;
	const MeshPtr merged = CreateMergedMesh("test/mesh_merge/Merged", copy);
	REQUIRE(merged);
	REQUIRE(merged->GetSubMeshCount() == 1);
	String name;
	CHECK(merged->GetSubMeshName(0, name));
	CHECK(name == "m0");
	REQUIRE(merged->GetSubMesh(0).GetMaterial());
	CHECK(merged->GetSubMesh(0).GetMaterial()->GetName() == QuadMaterial);

	MeshMergeGeometry readBack;
	REQUIRE(ReadSubMeshGeometry(merged->GetSubMesh(0), readBack));
	REQUIRE(readBack.vertices.size() == 8);
	CheckVector(readBack.vertices[5].pos, second * source[1].pos);
	CHECK(readBack.indices == bucket.indices);
}

TEST_CASE("A mirrored placement flips the triangle winding", "[mesh_merge]")
{
	const MeshPtr quad = QuadMesh("test/mesh_merge/Quad.hmsh");

	Matrix4 mirror;
	mirror.MakeTransform(Vector3::Zero, Vector3(-1.0f, 1.0f, 1.0f), Quaternion::Identity);

	MeshReadBackCache cache;
	MeshMergeAccumulator plain(cache);
	MeshMergeAccumulator mirrored(cache);
	REQUIRE(plain.Add(*quad, Matrix4::Identity));
	REQUIRE(mirrored.Add(*quad, mirror));

	const MeshMergeBucket& a = plain.GetParts()[0].begin()->second;
	const MeshMergeBucket& b = mirrored.GetParts()[0].begin()->second;

	// The source winding faces +Y; mirroring X alone would turn it to -Y, the re-wind keeps it +Y
	CheckVector(FaceNormal(a.vertices, a.indices, 0), Vector3::UnitY);
	CheckVector(FaceNormal(b.vertices, b.indices, 0), Vector3::UnitY);
	CheckVector(FaceNormal(b.vertices, b.indices, 1), Vector3::UnitY);

	CHECK(a.indices == std::vector<uint32>{ 0, 2, 1, 0, 3, 2 });
	CHECK(b.indices == std::vector<uint32>{ 0, 1, 2, 0, 2, 3 });
	CheckVector(b.vertices[1].pos, Vector3(-1.0f, 0.0f, 0.0f));
}

TEST_CASE("Material overrides split submeshes into their own buckets", "[mesh_merge]")
{
	const MeshPtr quad = QuadMesh("test/mesh_merge/TwoSubMeshes.hmsh", true);
	MaterialManager::Get().CreateManual("test/mesh_merge/Replacement.hmat");

	MeshReadBackCache cache;
	MeshMergeAccumulator accumulator(cache);
	REQUIRE(accumulator.Add(*quad, Matrix4::Identity, { "test/mesh_merge/Replacement.hmat", "" }));

	REQUIRE(accumulator.GetParts().size() == 1);
	const MeshMergeBuckets& buckets = accumulator.GetParts()[0];
	REQUIRE(buckets.size() == 2);
	CHECK(buckets.count("test/mesh_merge/Replacement.hmat") == 1);
	CHECK(buckets.count(OverrideMaterial) == 1);
	CHECK(buckets.count(QuadMaterial) == 0);
	REQUIRE(buckets.at("test/mesh_merge/Replacement.hmat").material);
	CHECK(buckets.at("test/mesh_merge/Replacement.hmat").material->GetName() == "test/mesh_merge/Replacement.hmat");
}

TEST_CASE("A bucket above the vertex budget continues in the next part", "[mesh_merge]")
{
	const MeshPtr quad = QuadMesh("test/mesh_merge/Quad.hmsh");

	MeshReadBackCache cache;
	MeshMergeAccumulator accumulator(cache, 6);
	REQUIRE(accumulator.Add(*quad, Matrix4::Identity));
	REQUIRE(accumulator.Add(*quad, Matrix4::GetTrans(Vector3(2.0f, 0.0f, 0.0f))));
	REQUIRE(accumulator.Add(*quad, Matrix4::GetTrans(Vector3(4.0f, 0.0f, 0.0f))));

	const auto& parts = accumulator.GetParts();
	REQUIRE(parts.size() == 3);
	for (const auto& part : parts)
	{
		REQUIRE(part.size() == 1);
		CHECK(part.begin()->second.vertices.size() == 4);
		CHECK(part.begin()->second.indices == std::vector<uint32>{ 0, 2, 1, 0, 3, 2 });
	}
	CheckVector(parts[2].begin()->second.vertices[0].pos, Vector3(4.0f, 0.0f, 0.0f));
}

TEST_CASE("Merged geometry never holds non-finite values", "[mesh_merge]")
{
	const MeshPtr quad = QuadMesh("test/mesh_merge/Quad.hmsh");

	MeshReadBackCache cache;
	MeshMergeAccumulator accumulator(cache);

	// A non-finite placement is refused outright
	Matrix4 broken = Matrix4::Identity;
	broken[0][3] = std::nanf("");
	CHECK_FALSE(accumulator.Add(*quad, broken));
	CHECK(accumulator.GetParts().empty());

	// A flattening (singular) scale has no inverse transpose; the normals stay unit length and finite
	Matrix4 flat;
	flat.MakeTransform(Vector3::Zero, Vector3(1.0f, 0.0f, 1.0f), Quaternion::Identity);
	REQUIRE(accumulator.Add(*quad, flat));
	REQUIRE(accumulator.GetParts().size() == 1);
	for (const auto& v : accumulator.GetParts()[0].begin()->second.vertices)
	{
		CHECK(std::isfinite(v.pos.x));
		CHECK(std::isfinite(v.pos.y));
		CHECK(std::isfinite(v.pos.z));
		CHECK(v.normal.GetLength() == Approx(1.0f));
		CHECK(v.tangent.GetLength() == Approx(1.0f));
		CHECK(v.binormal.GetLength() == Approx(1.0f));
	}
}

TEST_CASE("A merged mesh survives a save and load", "[mesh_merge]")
{
	const MeshPtr quad = QuadMesh("test/mesh_merge/Quad.hmsh");

	Matrix4 placed;
	placed.MakeTransform(Vector3(3.0f, 1.0f, -2.0f), Vector3(-2.0f, 1.0f, 1.0f), Quaternion::Identity);

	MeshReadBackCache cache;
	MeshMergeAccumulator accumulator(cache);
	REQUIRE(accumulator.Add(*quad, placed));
	const std::vector<uint32> expectedIndices = accumulator.GetParts()[0].begin()->second.indices;
	const MeshPtr merged = CreateMergedMesh("test/mesh_merge/Saved", accumulator.GetParts()[0]);
	REQUIRE(merged);

	std::vector<char> buffer;
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		MeshSerializer serializer;
		serializer.Serialize(merged, writer, mesh_version::Latest);
	}

	auto loaded = std::make_shared<Mesh>("test/mesh_merge/Loaded");
	{
		io::MemorySource source{ buffer };
		io::Reader reader{ source };
		MeshDeserializer deserializer{ *loaded };
		REQUIRE(deserializer.Read(reader));
	}

	REQUIRE(loaded->GetSubMeshCount() == 1);
	String name;
	CHECK(loaded->GetSubMeshName(0, name));
	CHECK(name == "m0");
	REQUIRE(loaded->GetSubMesh(0).GetMaterial());
	CHECK(loaded->GetSubMesh(0).GetMaterial()->GetName() == QuadMaterial);
	REQUIRE(CanMergeMesh(*loaded));

	MeshMergeGeometry geometry;
	REQUIRE(ReadSubMeshGeometry(loaded->GetSubMesh(0), geometry));
	REQUIRE(geometry.vertices.size() == 4);
	const std::vector<WorldModelMergeVertex> source = QuadVertices();
	for (size_t i = 0; i < 4; ++i)
	{
		CheckVector(geometry.vertices[i].pos, placed * source[i].pos);
		CheckVector(geometry.vertices[i].normal, Vector3::UnitY);
	}
	CHECK(geometry.indices == expectedIndices);
}
