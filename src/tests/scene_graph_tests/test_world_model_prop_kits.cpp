// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"
#include "graphics/material.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/prop_kit.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/sub_entity.h"
#include "scene_graph/sub_mesh.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_instance.h"
#include "scene_graph/world_model_manager.h"

#include <memory>
#include <vector>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	const String KitMeshPath = "test/prop_kits/Crate.hmsh";
	const String KitMeshMaterial = "test/prop_kits/Crate.hmat";
	const String KitOverrideMaterial = "test/prop_kits/Painted.hmat";

	/// Registers a one-submesh mesh with the mesh manager so world model instances resolve it
	/// without touching the asset registry. The mesh manager has no removal, so the mesh persists
	/// across tests under a test-only name.
	void EnsureKitMesh()
	{
		const MeshPtr mesh = MeshManager::Get().CreateManual(KitMeshPath);
		if (mesh->GetSubMeshCount() == 0)
		{
			mesh->CreateSubMesh("Crate").SetMaterial(MaterialManager::Get().CreateManual(KitMeshMaterial));
		}
	}

	WorldModelMeshRef MakeProp(const Vector3& position, const String& materialOverride = "")
	{
		WorldModelMeshRef prop;
		prop.meshPath = KitMeshPath;
		prop.position = position;
		prop.materialOverride = materialOverride;
		prop.name = "Crate";
		return prop;
	}

	WorldModelLight MakeLight(const Vector3& position)
	{
		WorldModelLight light {};
		light.type = WorldModelLight::LightType::Omni;
		light.position = position;
		light.color = 0xFFFFCC88;
		light.intensity = 2.0f;
		return light;
	}

	PropKitPtr MakeKit(const std::vector<Vector3>& propPositions, const size_t lightCount)
	{
		auto kit = std::make_shared<PropKit>();
		kit->name = "Test Kit";
		for (const auto& position : propPositions)
		{
			kit->props.push_back(MakeProp(position));
		}
		for (size_t i = 0; i < lightCount; ++i)
		{
			kit->lights.push_back(MakeLight(Vector3(0.0f, 2.0f, static_cast<float>(i))));
		}
		return kit;
	}

	/// A shell with one room spanning x/z -10..10 and y 0..5 and one mesh reference of its own.
	class TestShell final
	{
	public:
		explicit TestShell(const String& name)
			: m_name(name)
		{
			EnsureNullDevice();
			EnsureKitMesh();

			m_model = WorldModelManager::Get().CreateManual(name);
			WorldModelGroup& group = m_model->AddGroup();

			const AABB room(Vector3(-10.0f, 0.0f, -10.0f), Vector3(10.0f, 5.0f, 10.0f));
			group.SetBoundingBox(room);
			group.AddContainmentVolume(ContainmentVolume::FromAABB(room, "Room"));

			WorldModelMeshRef wall;
			wall.meshPath = KitMeshPath;
			wall.position = Vector3(0.0f, 0.0f, -9.0f);
			wall.name = "Wall";
			group.AddMeshRef(wall);
		}

		~TestShell()
		{
			WorldModelManager::Get().Remove(m_name);
		}

		const WorldModelPtr& GetModel() const { return m_model; }

	private:
		String m_name;
		WorldModelPtr m_model;
	};

	/// Counts the scene entities whose name marks them as created for a kit prop.
	size_t CountEntitiesContaining(const Scene& scene, const String& marker)
	{
		size_t count = 0;
		for (const Entity* entity : scene.GetAllEntities())
		{
			if (entity->GetName().find(marker) != String::npos)
			{
				++count;
			}
		}
		return count;
	}
}

TEST_CASE("Kit props and lights are created when the instance attaches", "[world_model][prop_kit]")
{
	TestShell shell("test/prop_kits/attach.hwmo");
	auto scene = std::make_unique<OctreeScene>();
	{
		WorldModelInstance instance("KitAttach", shell.GetModel());
		instance.SetPropKits({ MakeKit({ Vector3(1.0f, 0.0f, 1.0f), Vector3(-1.0f, 0.0f, 1.0f) }, 1) });

		// Before attach the kits are only stored
		CHECK(instance.GetKitPropInstanceCount() == 0);
		CHECK(instance.GetKitLightCount() == 0);

		SceneNode* node = scene->GetRootSceneNode().CreateChildSceneNode("KitAttachNode");
		node->AttachObject(instance);

		CHECK(instance.GetKitPropInstanceCount() == 2);
		CHECK(instance.GetKitLightCount() == 1);

		instance.Destroy(*scene);
		CHECK(instance.GetKitPropInstanceCount() == 0);
		CHECK(instance.GetKitLightCount() == 0);
		node->DetachObject(instance);
	}
}

TEST_CASE("Changing kits after attach rebuilds only the kit content", "[world_model][prop_kit]")
{
	TestShell shell("test/prop_kits/rebuild.hwmo");
	auto scene = std::make_unique<OctreeScene>();
	{
		WorldModelInstance instance("KitRebuild", shell.GetModel());
		instance.SetPropKits({ MakeKit({ Vector3(1.0f, 0.0f, 1.0f), Vector3(-1.0f, 0.0f, 1.0f) }, 1) });

		SceneNode* node = scene->GetRootSceneNode().CreateChildSceneNode("KitRebuildNode");
		node->AttachObject(instance);
		REQUIRE(instance.GetKitPropInstanceCount() == 2);

		const size_t shellEntities = CountEntitiesContaining(*scene, "KitRebuild_group");
		REQUIRE(shellEntities == 1);

		instance.SetPropKits({});
		CHECK(instance.GetKitPropInstanceCount() == 0);
		CHECK(instance.GetKitLightCount() == 0);
		CHECK(CountEntitiesContaining(*scene, "KitRebuild_group") == shellEntities);
		CHECK(shell.GetModel()->GetGroup(0)->GetMeshRefs().size() == 1);

		// And back again
		instance.SetPropKits({ MakeKit({ Vector3(2.0f, 0.0f, 2.0f) }, 2) });
		CHECK(instance.GetKitPropInstanceCount() == 1);
		CHECK(instance.GetKitLightCount() == 2);
		CHECK(CountEntitiesContaining(*scene, "KitRebuild_group") == shellEntities);

		instance.Destroy(*scene);
		node->DetachObject(instance);
	}
}

TEST_CASE("Two instances share one kit", "[world_model][prop_kit]")
{
	TestShell shell("test/prop_kits/shared.hwmo");
	const PropKitPtr kit = MakeKit({ Vector3(1.0f, 0.0f, 1.0f), Vector3(-1.0f, 0.0f, 1.0f) }, 0);

	auto scene = std::make_unique<OctreeScene>();
	{
		WorldModelInstance first("KitShareA", shell.GetModel());
		WorldModelInstance second("KitShareB", shell.GetModel());
		first.SetPropKits({ kit });
		second.SetPropKits({ kit });

		SceneNode* firstNode = scene->GetRootSceneNode().CreateChildSceneNode("KitShareANode", Vector3(0.0f, 0.0f, 0.0f));
		SceneNode* secondNode = scene->GetRootSceneNode().CreateChildSceneNode("KitShareBNode", Vector3(100.0f, 0.0f, 0.0f));
		firstNode->AttachObject(first);
		secondNode->AttachObject(second);

		CHECK(first.GetKitPropInstanceCount() == 2);
		CHECK(second.GetKitPropInstanceCount() == 2);

		first.Destroy(*scene);
		second.Destroy(*scene);
		firstNode->DetachObject(first);
		secondNode->DetachObject(second);
	}
}

TEST_CASE("A kit prop outside every room is never culled", "[world_model][prop_kit]")
{
	TestShell shell("test/prop_kits/rooms.hwmo");
	auto scene = std::make_unique<OctreeScene>();
	{
		WorldModelInstance instance("KitRooms", shell.GetModel());
		instance.SetPropKits({ MakeKit({ Vector3(1.0f, 1.0f, 1.0f), Vector3(50.0f, 1.0f, 0.0f) }, 0) });

		SceneNode* node = scene->GetRootSceneNode().CreateChildSceneNode("KitRoomsNode");
		node->AttachObject(instance);

		REQUIRE(instance.GetKitPropInstanceCount() == 2);
		CHECK(instance.GetKitPropGroup(0) == 0);
		CHECK(instance.GetKitPropGroup(1) == -1);
		CHECK(instance.GetKitPropGroup(2) == -1);

		instance.Destroy(*scene);
		node->DetachObject(instance);
	}
}

TEST_CASE("Unbatched kit props use their own transform and material override", "[world_model][prop_kit]")
{
	TestShell shell("test/prop_kits/override.hwmo");
	const MaterialPtr overrideMaterial = MaterialManager::Get().CreateManual(KitOverrideMaterial);

	auto scene = std::make_unique<OctreeScene>();
	{
		auto kit = std::make_shared<PropKit>();
		kit->props.push_back(MakeProp(Vector3(3.0f, 0.5f, -2.0f), KitOverrideMaterial));

		WorldModelInstance instance("KitOverride", shell.GetModel());
		instance.SetBatchingEnabled(false);
		instance.SetPropKits({ kit });

		SceneNode* node = scene->GetRootSceneNode().CreateChildSceneNode("KitOverrideNode");
		node->AttachObject(instance);
		REQUIRE(instance.GetKitPropInstanceCount() == 1);

		Entity* propEntity = nullptr;
		for (Entity* entity : scene->GetAllEntities())
		{
			if (entity->GetName().find("KitOverride_kitprop") != String::npos)
			{
				propEntity = entity;
			}
		}
		REQUIRE(propEntity != nullptr);
		REQUIRE(propEntity->GetParentSceneNode() != nullptr);

		const Vector3 position = propEntity->GetParentSceneNode()->GetPosition();
		CHECK(position.x == Approx(3.0f));
		CHECK(position.y == Approx(0.5f));
		CHECK(position.z == Approx(-2.0f));

		REQUIRE(propEntity->GetSubEntity(0) != nullptr);
		const MaterialPtr material = propEntity->GetSubEntity(0)->GetMaterial();
		REQUIRE(material != nullptr);
		CHECK(String(material->GetName()) == KitOverrideMaterial);

		instance.Destroy(*scene);
		node->DetachObject(instance);
	}

	MaterialManager::Get().Remove(KitOverrideMaterial);
}
