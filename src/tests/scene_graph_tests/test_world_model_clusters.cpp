// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"
#include "graphics/graphics_device.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/sub_mesh.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_instance.h"
#include "scene_graph/world_model_manager.h"
#include "scene_graph/world_model_merge.h"

#include <memory>
#include <vector>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	const String TriangleMeshPath = "test/clusters/Triangle.hmsh";

	/// Scale that makes the unit triangle structure rather than detail (see IsWorldModelDetail).
	constexpr float StructureScale = 4.0f;

	/// Registers a one-triangle mesh in the static mesh layout, so it can be merged.
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
		subMesh.SetMaterial(MaterialManager::Get().CreateManual("test/clusters/Triangle.hmat"));

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

	WorldModelMeshRef Triangle(const Vector3& position)
	{
		WorldModelMeshRef ref;
		ref.meshPath = TriangleMeshPath;
		ref.name = "Triangle";
		ref.position = position;
		ref.scale = Vector3(StructureScale, StructureScale, StructureScale);
		return ref;
	}

	/// A town: an outdoor group with triangles in two cluster cells and a room, plus a house child
	/// world model (one outdoor group) standing in the first cell. Removed again on destruction.
	struct TownFixture
	{
		const String townPath = "test/clusters/Town.hwmo";
		const String housePath = "test/clusters/House.hwmo";
		WorldModelPtr town;
		WorldModelPtr house;

		TownFixture()
		{
			EnsureTriangleMesh();

			house = WorldModelManager::Get().CreateManual(housePath);
			WorldModelGroup& houseOutside = house->AddGroup();
			houseOutside.SetFlags(WorldModelGroupFlags::Exterior);
			houseOutside.AddMeshRef(Triangle(Vector3(1.0f, 0.0f, 1.0f)));

			town = WorldModelManager::Get().CreateManual(townPath);
			WorldModelGroup& streets = town->AddGroup();
			streets.SetFlags(WorldModelGroupFlags::Exterior);
			streets.AddMeshRef(Triangle(Vector3(0.0f, 0.0f, 0.0f)));
			streets.AddMeshRef(Triangle(Vector3(WorldModelClusterSize + 10.0f, 0.0f, 0.0f)));

			WorldModelGroup& room = town->AddGroup();
			room.SetFlags(WorldModelGroupFlags::Interior);
			room.AddMeshRef(Triangle(Vector3(5.0f, 0.0f, 5.0f)));

			WorldModelChildRef child;
			child.wmoPath = housePath;
			child.name = "House";
			child.position = Vector3(10.0f, 0.0f, 10.0f);
			town->AddChildRef(child);
		}

		~TownFixture()
		{
			WorldModelManager::Get().Remove(townPath);
			WorldModelManager::Get().Remove(housePath);
		}
	};

	bool StartsWith(const String& text, const String& prefix)
	{
		return text.rfind(prefix, 0) == 0;
	}
}

TEST_CASE("Only groups that are not rooms are clustered", "[world_model_merge][clusters]")
{
	WorldModelGroup outdoors;
	outdoors.SetFlags(WorldModelGroupFlags::Exterior);
	CHECK(WorldModelInstance::IsClusteredGroup(outdoors));

	WorldModelGroup unflagged;
	CHECK(WorldModelInstance::IsClusteredGroup(unflagged));

	WorldModelGroup interior;
	interior.SetFlags(WorldModelGroupFlags::Interior);
	CHECK_FALSE(WorldModelInstance::IsClusteredGroup(interior));

	WorldModelGroup both;
	both.SetFlags(WorldModelGroupFlags::Interior | WorldModelGroupFlags::Exterior);
	CHECK_FALSE(WorldModelInstance::IsClusteredGroup(both));

	// A dungeon room defined by its volume is a room without any flag
	WorldModelGroup volumeRoom;
	volumeRoom.AddContainmentVolume(ContainmentVolume{});
	CHECK_FALSE(WorldModelInstance::IsClusteredGroup(volumeRoom));
}

TEST_CASE("Cluster cells are WorldModelClusterSize squares centred on their origin", "[world_model_merge][clusters]")
{
	CHECK(GetWorldModelClusterCell(Vector3(0.0f, 50.0f, 0.0f)) == std::make_pair(0, 0));
	CHECK(GetWorldModelClusterCell(Vector3(WorldModelClusterSize + 1.0f, 0.0f, -1.0f)) == std::make_pair(1, -1));

	const Vector3 origin = GetWorldModelClusterOrigin({ 1, -1 });
	CHECK(origin.x == Approx(WorldModelClusterSize * 1.5f));
	CHECK(origin.y == Approx(0.0f));
	CHECK(origin.z == Approx(WorldModelClusterSize * -0.5f));
}

TEST_CASE("Cluster geometry can be built a group at a time", "[world_model_merge][clusters]")
{
	const TownFixture fixture;

	// Two clustered groups: the streets and the house outside. The room is not part of it.
	CHECK(WorldModelInstance::PrewarmClusterGeometry(fixture.town, 1) == nullptr);

	const auto clusters = WorldModelInstance::PrewarmClusterGeometry(fixture.town, 1);
	REQUIRE(clusters != nullptr);
	CHECK(WorldModelInstance::PrewarmClusterGeometry(fixture.town) == clusters);
}

TEST_CASE("The top-level instance draws the outdoors of the whole tree per cluster cell", "[world_model_merge][clusters]")
{
	const TownFixture fixture;

	auto scene = std::make_unique<OctreeScene>();
	{
		WorldModelInstance town("ClusterTown", fixture.town);
		town.SetMergingEnabled(true);

		SceneNode* node = scene->GetRootSceneNode().CreateChildSceneNode("ClusterTownNode");
		node->SetPosition(Vector3(100.0f, 0.0f, 0.0f));
		node->AttachObject(town);

		// One cell holds the first street triangle and the house, the other the second street triangle
		CHECK(town.GetClusterEntityCount() == 2);
		REQUIRE(town.GetChildInstanceCount() == 1);
		CHECK(town.GetChildInstance(0)->GetClusterEntityCount() == 0);

		uint32 clusterVertices = 0;
		uint32 groupMeshes = 0;
		bool houseInCluster = false;
		for (Entity* entity : scene->GetAllEntities())
		{
			const String meshName(entity->GetMesh()->GetName());
			if (StartsWith(meshName, "WorldModelCluster_"))
			{
				for (uint16 i = 0; i < entity->GetMesh()->GetSubMeshCount(); ++i)
				{
					clusterVertices += entity->GetMesh()->GetSubMesh(i).vertexData->vertexCount;
				}

				// The house's triangle, through the child placement and the town's node: its far corner
				// lies at 100 + 10 + 1 + 4 on X
				const AABB& bounds = entity->GetWorldBoundingBox(true);
				if (bounds.max.x == Approx(115.0f) && bounds.max.z == Approx(15.0f))
				{
					houseInCluster = true;
				}
			}
			else if (StartsWith(meshName, "WorldModelMerge_"))
			{
				++groupMeshes;
			}
		}

		// Three outdoor triangles in the clusters, nothing outdoors merged twice; only the room merged per group
		CHECK(clusterVertices == 9);
		CHECK(groupMeshes == 1);
		CHECK(houseInCluster);

		town.Destroy(*scene);
	}
}
