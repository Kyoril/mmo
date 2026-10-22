// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"
#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/particle_emitter_serializer.h"
#include "scene_graph/prop_kit.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/sub_mesh.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_instance.h"
#include "scene_graph/world_model_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	const String FlamePath = "Effects/Flame.hpar";
	const String BowlMeshPath = "test/wmo_particles/Bowl.hmsh";

	/// An asset root in the temp directory holding one two-emitter particle system. The registry
	/// is torn down again on destruction.
	struct ParticleAssetFixture
	{
		std::filesystem::path root;

		ParticleAssetFixture()
		{
			root = std::filesystem::temp_directory_path() / "mmo_wmo_particles_test";
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root / "Effects");

			ParticleSystemParameters system;
			system.emitters.resize(2);
			system.emitters[0].name = "Flame";
			system.emitters[1].name = "Embers";

			{
				std::ofstream file(root / FlamePath, std::ios::binary);
				io::StreamSink sink(file);
				io::Writer writer(sink);
				ParticleSystemSerializer().Serialize(system, writer);
				sink.Flush();
			}

			AssetRegistry::Initialize(root, {});
		}

		~ParticleAssetFixture()
		{
			AssetRegistry::Destroy();
			std::filesystem::remove_all(root);
		}
	};

	void EnsureBowlMesh()
	{
		const MeshPtr bowl = MeshManager::Get().CreateManual(BowlMeshPath);
		if (bowl->GetSubMeshCount() == 0)
		{
			bowl->CreateSubMesh("Bowl").SetMaterial(MaterialManager::Get().CreateManual("test/wmo_particles/Bowl.hmat"));
		}
	}

	WorldModelMeshRef Ref(const String& path, const Vector3& position)
	{
		WorldModelMeshRef ref;
		ref.meshPath = path;
		ref.position = position;
		return ref;
	}
}

TEST_CASE("World model instances turn particle system references into looping particle systems", "[world_model][particles]")
{
	EnsureNullDevice();
	EnsureBowlMesh();
	const ParticleAssetFixture assets;

	const String modelName = "test/wmo_particles/brazier.hwmo";
	const WorldModelPtr model = WorldModelManager::Get().CreateManual(modelName);
	WorldModelGroup& group = model->AddGroup();
	group.AddMeshRef(Ref(BowlMeshPath, Vector3::Zero));
	group.AddMeshRef(Ref(FlamePath, Vector3(0.0f, 1.0f, 0.0f)));

	WorldModelMeshRef hidden = Ref(FlamePath, Vector3(0.0f, 0.0f, -3.0f));
	hidden.visible = false;
	group.AddMeshRef(hidden);

	auto kit = std::make_shared<PropKit>();
	kit->props.push_back(Ref(FlamePath, Vector3(2.0f, 0.0f, 0.0f)));

	const bool batching = GENERATE(false, true);

	auto scene = std::make_unique<OctreeScene>();
	{
		WorldModelInstance instance("ParticleShell", model);
		instance.SetBatchingEnabled(batching);
		instance.SetPropKits({ kit });

		SceneNode* node = scene->GetRootSceneNode().CreateChildSceneNode("ParticleShellNode");
		node->SetPosition(Vector3(100.0f, 0.0f, 0.0f));
		node->AttachObject(instance);

		// The visible group reference and the kit prop; the hidden reference stays out
		REQUIRE(instance.GetParticleSystemCount() == 2);

		const std::vector<ParticleEmitter*> systems = scene->GetAllParticleEmitters();
		REQUIRE(systems.size() == 2);
		for (const ParticleEmitter* system : systems)
		{
			// The whole system was loaded, not just its first emitter
			CHECK(system->GetEmitterCount() == 2);
			CHECK(system->IsPlaying());
		}

		const auto placedAt = [&systems](const Vector3& position)
		{
			for (const ParticleEmitter* system : systems)
			{
				if (system->GetDerivedPosition().GetSquaredDistanceTo(position) < 1.0e-4f)
				{
					return true;
				}
			}
			return false;
		};
		CHECK(placedAt(Vector3(100.0f, 1.0f, 0.0f)));   // group reference, through the placement
		CHECK(placedAt(Vector3(102.0f, 0.0f, 0.0f)));   // kit prop

		// Changing the kits rebuilds the particle systems without leaking the old ones
		instance.SetPropKits({});
		CHECK(instance.GetParticleSystemCount() == 1);
		CHECK(scene->GetAllParticleEmitters().size() == 1);

		instance.Destroy(*scene);
		CHECK(instance.GetParticleSystemCount() == 0);
		CHECK(scene->GetAllParticleEmitters().empty());
		node->DetachObject(instance);
	}

	WorldModelManager::Get().Remove(modelName);
}
