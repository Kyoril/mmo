// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"
#include "graphics/graphics_device.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/sub_mesh.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_instance.h"
#include "scene_graph/world_model_manager.h"
#include "scene_graph/world_model_merge.h"

#include <algorithm>
#include <memory>
#include <vector>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	const String TriangleMeshPath = "test/merge_cache/Triangle.hmsh";

	/// Bytes of merged geometry one placement of the triangle mesh adds to a group.
	constexpr size_t TriangleBytes = 3 * sizeof(WorldModelMergeVertex) + 3 * sizeof(uint16);

	/// Registers a one-triangle mesh in the static mesh layout, so it can be merged. The mesh manager
	/// has no removal, so the mesh persists across tests under a test-only name.
	void EnsureTriangleMesh()
	{
		const MeshPtr mesh = MeshManager::Get().CreateManual(TriangleMeshPath);
		if (mesh->GetSubMeshCount() > 0)
		{
			return;
		}

		GraphicsDevice& device = EnsureNullDevice();

		SubMesh& subMesh = mesh->CreateSubMesh("Triangle");
		subMesh.useSharedVertices = false;
		subMesh.SetMaterial(MaterialManager::Get().CreateManual("test/merge_cache/Triangle.hmat"));

		std::vector<WorldModelMergeVertex> vertices(3);
		vertices[0].pos = Vector3(0.0f, 0.0f, 0.0f);
		vertices[1].pos = Vector3(1.0f, 0.0f, 0.0f);
		vertices[2].pos = Vector3(0.0f, 0.0f, 1.0f);
		const std::vector<uint16> indices{ 0, 1, 2 };

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
		subMesh.indexData->indexCount = static_cast<uint32>(indices.size());
		subMesh.indexData->indexBuffer = device.CreateIndexBuffer(
			static_cast<uint32>(indices.size()), IndexBufferSize::Index_16, BufferUsage::StaticWriteOnly, indices.data());

		mesh->SetBounds(AABB(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 1.0f)));
	}

	/// A manual world model whose groups each hold one triangle placement; removed again on destruction.
	class TriangleModel final
	{
	public:
		TriangleModel(const String& name, const size_t groupCount)
			: m_name(name)
		{
			EnsureTriangleMesh();

			m_model = WorldModelManager::Get().CreateManual(name);
			for (size_t i = 0; i < groupCount; ++i)
			{
				WorldModelMeshRef ref;
				ref.meshPath = TriangleMeshPath;
				ref.position = Vector3(static_cast<float>(i) * 10.0f, 0.0f, 0.0f);
				ref.name = "Triangle";
				m_model->AddGroup().AddMeshRef(ref);
			}
		}

		~TriangleModel()
		{
			WorldModelManager::Get().Remove(m_name);
		}

		const WorldModelPtr& Get() const { return m_model; }

	private:
		String m_name;
		WorldModelPtr m_model;
	};

	/// Restores the default (no retention) when a test ends, however it ends.
	struct RetentionGuard final
	{
		explicit RetentionGuard(const size_t bytes)
		{
			WorldModelInstance::SetMergedGeometryRetention(bytes);
		}

		~RetentionGuard()
		{
			WorldModelInstance::SetMergedGeometryRetention(0);
		}
	};
}

TEST_CASE("Merged geometry can be built a group at a time", "[world_model_merge][merge_cache]")
{
	const TriangleModel model("test/merge_cache/Stepped.hwmo", 3);

	CHECK(WorldModelInstance::PrewarmMergedGeometry(model.Get(), 1) == nullptr);
	CHECK(WorldModelInstance::PrewarmMergedGeometry(model.Get(), 1) == nullptr);

	const auto merged = WorldModelInstance::PrewarmMergedGeometry(model.Get(), 1);
	REQUIRE(merged != nullptr);

	// Complete now: further calls hand out the same geometry instead of merging again
	CHECK(WorldModelInstance::PrewarmMergedGeometry(model.Get(), 1) == merged);
	CHECK(WorldModelInstance::PrewarmMergedGeometry(model.Get()) == merged);
}

TEST_CASE("Merged geometry dies with its last user unless retained", "[world_model_merge][merge_cache]")
{
	const TriangleModel model("test/merge_cache/Retained.hwmo", 1);

	SECTION("Default: no retention")
	{
		auto merged = WorldModelInstance::PrewarmMergedGeometry(model.Get());
		REQUIRE(merged != nullptr);

		const std::weak_ptr<const void> weak = merged;
		merged.reset();
		CHECK(weak.expired());
	}

	SECTION("Retained until retention is turned off")
	{
		const RetentionGuard retention(1024 * 1024);

		auto merged = WorldModelInstance::PrewarmMergedGeometry(model.Get());
		REQUIRE(merged != nullptr);

		const std::weak_ptr<const void> weak = merged;
		merged.reset();
		CHECK_FALSE(weak.expired());

		// Streaming back in finds the retained geometry
		CHECK(WorldModelInstance::PrewarmMergedGeometry(model.Get()) == weak.lock());

		WorldModelInstance::SetMergedGeometryRetention(0);
		CHECK(weak.expired());
	}
}

TEST_CASE("Unused merged geometry beyond the budget is released oldest first", "[world_model_merge][merge_cache]")
{
	// Room for one released model's geometry, not two
	const RetentionGuard retention(TriangleBytes + TriangleBytes / 2);

	const TriangleModel first("test/merge_cache/First.hwmo", 1);
	const TriangleModel second("test/merge_cache/Second.hwmo", 1);
	const TriangleModel third("test/merge_cache/Third.hwmo", 1);

	std::weak_ptr<const void> firstWeak = WorldModelInstance::PrewarmMergedGeometry(first.Get());
	std::weak_ptr<const void> secondWeak = WorldModelInstance::PrewarmMergedGeometry(second.Get());
	CHECK_FALSE(firstWeak.expired());
	CHECK_FALSE(secondWeak.expired());

	// The third merge trims: two unused models exceed the budget, so the older one goes
	const auto thirdMerged = WorldModelInstance::PrewarmMergedGeometry(third.Get());
	REQUIRE(thirdMerged != nullptr);
	CHECK(firstWeak.expired());
	CHECK_FALSE(secondWeak.expired());
}

TEST_CASE("Geometry still in use is never released by the budget", "[world_model_merge][merge_cache]")
{
	const RetentionGuard retention(1);

	const TriangleModel first("test/merge_cache/InUseFirst.hwmo", 1);
	const TriangleModel second("test/merge_cache/InUseSecond.hwmo", 1);

	const auto firstMerged = WorldModelInstance::PrewarmMergedGeometry(first.Get());
	const auto secondMerged = WorldModelInstance::PrewarmMergedGeometry(second.Get());
	REQUIRE(firstMerged != nullptr);
	REQUIRE(secondMerged != nullptr);

	// Both are held, so a budget smaller than either releases neither
	CHECK(WorldModelInstance::PrewarmMergedGeometry(first.Get()) == firstMerged);
	CHECK(WorldModelInstance::PrewarmMergedGeometry(second.Get()) == secondMerged);
}

TEST_CASE("Mesh dependencies cover visible mesh references and the default doodad set", "[world_model_merge][merge_cache]")
{
	WorldModel model;

	WorldModelGroup& group = model.AddGroup();
	WorldModelMeshRef visible;
	visible.meshPath = "Visible.hmsh";
	group.AddMeshRef(visible);

	WorldModelMeshRef hidden;
	hidden.meshPath = "Hidden.hmsh";
	hidden.visible = false;
	group.AddMeshRef(hidden);

	model.GetDoodadNames() = { "DefaultDoodad.hmsh", "OptionalDoodad.hmsh" };
	model.GetDoodads().push_back(WorldModelDoodad{ 0, Vector3::Zero, Quaternion::Identity, 1.0f, 0xFFFFFFFF });
	model.GetDoodads().push_back(WorldModelDoodad{ 1, Vector3::Zero, Quaternion::Identity, 1.0f, 0xFFFFFFFF });
	model.GetDoodadSets().push_back(WorldModelDoodadSet{ "Default", 0, 1 });
	model.GetDoodadSets().push_back(WorldModelDoodadSet{ "Optional", 1, 1 });

	std::vector<String> meshPaths;
	WorldModelInstance::CollectMeshDependencies(model, meshPaths);

	CHECK(std::find(meshPaths.begin(), meshPaths.end(), "Visible.hmsh") != meshPaths.end());
	CHECK(std::find(meshPaths.begin(), meshPaths.end(), "DefaultDoodad.hmsh") != meshPaths.end());
	CHECK(std::find(meshPaths.begin(), meshPaths.end(), "Hidden.hmsh") == meshPaths.end());
	CHECK(std::find(meshPaths.begin(), meshPaths.end(), "OptionalDoodad.hmsh") == meshPaths.end());
}
