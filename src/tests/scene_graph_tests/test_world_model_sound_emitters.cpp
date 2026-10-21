// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/prop_kit.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/sub_mesh.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_instance.h"
#include "scene_graph/world_model_manager.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	const String TorchMeshPath = "test/mesh_sounds/Torch.hmsh";
	const String SilentMeshPath = "test/mesh_sounds/Wall.hmsh";

	/// A torch mesh whose flame sound sits 1 m above its origin, and a mesh without sounds.
	void EnsureMeshes()
	{
		const MeshPtr torch = MeshManager::Get().CreateManual(TorchMeshPath);
		if (torch->GetSubMeshCount() == 0)
		{
			torch->CreateSubMesh("Torch").SetMaterial(MaterialManager::Get().CreateManual("test/mesh_sounds/Torch.hmat"));

			MeshSoundEmitter flame;
			flame.soundEntryId = 42;
			flame.offset = Vector3(0.0f, 1.0f, 0.0f);
			flame.volume = 0.5f;
			flame.maxDistance = 8.0f;
			torch->SetSoundEmitters({ flame });
		}

		const MeshPtr wall = MeshManager::Get().CreateManual(SilentMeshPath);
		if (wall->GetSubMeshCount() == 0)
		{
			wall->CreateSubMesh("Wall").SetMaterial(MaterialManager::Get().CreateManual("test/mesh_sounds/Wall.hmat"));
		}
	}

	WorldModelMeshRef MeshRef(const String& path, const Vector3& position)
	{
		WorldModelMeshRef ref;
		ref.meshPath = path;
		ref.position = position;
		return ref;
	}

	bool Contains(const std::vector<PlacedSoundEmitter>& emitters, const Vector3& position)
	{
		for (const PlacedSoundEmitter& emitter : emitters)
		{
			if (emitter.position.GetSquaredDistanceTo(position) < 1.0e-4f)
			{
				return true;
			}
		}
		return false;
	}
}

TEST_CASE("Mesh placement resolves emitter offsets through rotation and scale", "[mesh_sounds]")
{
	EnsureNullDevice();
	EnsureMeshes();

	// Quarter turn around Z: the +Y offset points to -X
	const Quaternion quarterTurn(Radian(1.57079633f), Vector3::UnitZ);

	std::vector<PlacedSoundEmitter> placed;
	AppendPlacedSoundEmitters(*MeshManager::Get().Load(TorchMeshPath), Vector3(10.0f, 0.0f, 0.0f), quarterTurn, Vector3(2.0f, 2.0f, 2.0f), placed);

	REQUIRE(placed.size() == 1);
	CHECK(placed[0].soundEntryId == 42);
	CHECK(placed[0].position.x == Approx(8.0f).margin(1.0e-4));
	CHECK(placed[0].position.y == Approx(0.0f).margin(1.0e-4));
	CHECK(placed[0].volume == Approx(0.5f));
	CHECK(placed[0].maxDistance == Approx(8.0f));
}

TEST_CASE("World model instances collect the emitters of shell, kit props and children", "[world_model][mesh_sounds]")
{
	EnsureNullDevice();
	EnsureMeshes();

	const String childName = "test/mesh_sounds/child.hwmo";
	const String shellName = "test/mesh_sounds/shell.hwmo";

	// Child world model with one torch of its own, placed 5 m along +Z in the shell
	const WorldModelPtr child = WorldModelManager::Get().CreateManual(childName);
	child->AddGroup().AddMeshRef(MeshRef(TorchMeshPath, Vector3::Zero));

	const WorldModelPtr shell = WorldModelManager::Get().CreateManual(shellName);
	WorldModelGroup& group = shell->AddGroup();
	group.AddMeshRef(MeshRef(TorchMeshPath, Vector3(1.0f, 0.0f, 0.0f)));
	group.AddMeshRef(MeshRef(SilentMeshPath, Vector3(-1.0f, 0.0f, 0.0f)));

	WorldModelMeshRef hidden = MeshRef(TorchMeshPath, Vector3(0.0f, 0.0f, -3.0f));
	hidden.visible = false;
	group.AddMeshRef(hidden);

	WorldModelChildRef childRef;
	childRef.wmoPath = childName;
	childRef.position = Vector3(0.0f, 0.0f, 5.0f);
	shell->AddChildRef(childRef);

	auto kit = std::make_shared<PropKit>();
	kit->props.push_back(MeshRef(TorchMeshPath, Vector3(2.0f, 0.0f, 0.0f)));

	auto scene = std::make_unique<OctreeScene>();
	{
		WorldModelInstance instance("SoundShell", shell);
		instance.SetPropKits({ kit });

		SceneNode* node = scene->GetRootSceneNode().CreateChildSceneNode("SoundShellNode");
		node->SetPosition(Vector3(100.0f, 0.0f, 0.0f));
		node->AttachObject(instance);

		std::vector<PlacedSoundEmitter> emitters;
		instance.CollectSoundEmitters(emitters);

		CHECK(emitters.size() == 3);
		CHECK(Contains(emitters, Vector3(101.0f, 1.0f, 0.0f)));   // shell mesh ref
		CHECK(Contains(emitters, Vector3(102.0f, 1.0f, 0.0f)));   // kit prop
		CHECK(Contains(emitters, Vector3(100.0f, 1.0f, 5.0f)));   // child world model

		instance.Destroy(*scene);
		node->DetachObject(instance);
	}

	WorldModelManager::Get().Remove(shellName);
	WorldModelManager::Get().Remove(childName);
}
