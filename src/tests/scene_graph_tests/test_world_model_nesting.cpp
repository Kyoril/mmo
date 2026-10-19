// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "memory_source.h"
#include "vector_sink.h"
#include "reader.h"
#include "writer.h"

#include "null_device.h"
#include "math/degree.h"
#include "math/quaternion.h"
#include "math/radian.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_instance.h"
#include "scene_graph/world_model_manager.h"
#include "scene_graph/world_model_serializer.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

using namespace mmo;
using mmo::test::EnsureNullDevice;

namespace
{
	/// Reads a shipped world model from data/client, or returns false when the submodule is missing.
	bool LoadShippedWorldModel(const String& path, WorldModel& out)
	{
		std::ifstream file(MMO_SOURCE_DIR "/data/client/" + path, std::ios::binary);
		if (!file)
		{
			return false;
		}

		const std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		io::MemorySource memory{ buffer.data(), buffer.data() + buffer.size() };
		io::Reader reader{ memory };
		WorldModelDeserializer deserializer{ out };
		return deserializer.Read(reader);
	}

	WorldModelChildRef MakeChildRef(const String& path, const Vector3& position, const Quaternion& rotation = Quaternion::Identity)
	{
		WorldModelChildRef ref;
		ref.wmoPath = path;
		ref.position = position;
		ref.rotation = rotation;
		ref.name = path;
		return ref;
	}

	/// Registers manual world models with the manager for one test and removes them afterwards, so
	/// WorldModelInstance resolves child references without touching the asset registry.
	class ManualModels final
	{
	public:
		~ManualModels()
		{
			for (const auto& name : m_names)
			{
				WorldModelManager::Get().Remove(name);
			}
		}

		WorldModelPtr Create(const String& name)
		{
			m_names.push_back(name);
			return WorldModelManager::Get().CreateManual(name);
		}

	private:
		std::vector<String> m_names;
	};
}

TEST_CASE("Child world model references survive a save and load", "[world_model_nesting]")
{
	WorldModel source;
	WorldModelChildRef ref = MakeChildRef("Models/Haven/House_A.hwmo", Vector3(12.0f, 0.5f, -3.0f),
		Quaternion(Radian(Degree(90.0f)), Vector3::UnitY));
	ref.scale = Vector3(1.0f, 2.0f, 1.0f);
	ref.name = "House A";
	ref.visible = false;
	source.AddChildRef(ref);

	std::vector<char> buffer;
	{
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		WorldModelSerializer serializer;
		serializer.Serialize(source, writer, world_model_version::Latest);
	}

	WorldModel loaded;
	io::MemorySource memory{ buffer.data(), buffer.data() + buffer.size() };
	io::Reader reader{ memory };
	WorldModelDeserializer deserializer{ loaded };
	REQUIRE(deserializer.Read(reader));
	REQUIRE(loaded.GetChildRefs().size() == 1);

	const WorldModelChildRef& actual = loaded.GetChildRefs()[0];
	CHECK(actual.wmoPath == ref.wmoPath);
	CHECK(actual.name == ref.name);
	CHECK(actual.position.x == Approx(12.0f));
	CHECK(actual.position.z == Approx(-3.0f));
	CHECK(actual.rotation.w == Approx(ref.rotation.w));
	CHECK(actual.rotation.y == Approx(ref.rotation.y));
	CHECK(actual.scale.y == Approx(2.0f));
	CHECK_FALSE(actual.visible);
}

TEST_CASE("World model instances instantiate their child world models", "[world_model_nesting]")
{
	EnsureNullDevice();
	ManualModels models;

	const WorldModelPtr town = models.Create("test/nesting/town.hwmo");
	const WorldModelPtr house = models.Create("test/nesting/house.hwmo");
	const WorldModelPtr lantern = models.Create("test/nesting/lantern.hwmo");

	town->AddChildRef(MakeChildRef("test/nesting/house.hwmo", Vector3(10.0f, 0.0f, 0.0f), Quaternion(Radian(Degree(90.0f)), Vector3::UnitY)));
	WorldModelChildRef hidden = MakeChildRef("test/nesting/house.hwmo", Vector3(-10.0f, 0.0f, 0.0f));
	hidden.visible = false;
	town->AddChildRef(hidden);
	house->AddChildRef(MakeChildRef("test/nesting/lantern.hwmo", Vector3(0.0f, 0.0f, 2.0f)));

	auto scene = std::make_unique<OctreeScene>();
	{
		WorldModelInstance instance("Town", town);
		instance.SetQueryFlags(0x41);

		SceneNode* node = scene->GetRootSceneNode().CreateChildSceneNode("TownNode", Vector3(100.0f, 0.0f, 0.0f));
		node->AttachObject(instance);

		// The hidden reference is skipped, the visible one becomes a full instance
		REQUIRE(instance.GetChildInstanceCount() == 1);
		WorldModelInstance* houseInstance = instance.GetChildInstance(0);
		REQUIRE(houseInstance != nullptr);
		CHECK(houseInstance->GetWorldModel() == house);
		CHECK(houseInstance->GetQueryFlags() == 0x41);

		// Nesting continues below the child
		REQUIRE(houseInstance->GetChildInstanceCount() == 1);
		WorldModelInstance* lanternInstance = houseInstance->GetChildInstance(0);
		REQUIRE(lanternInstance != nullptr);
		CHECK(lanternInstance->GetWorldModel() == lantern);

		// Transforms compose: town at x=100, house 10 further along x and turned 90 degrees about Y,
		// so the lantern's +2 z in house space ends up as +2 x in world space
		const Vector3 lanternPosition = lanternInstance->GetParentSceneNode()->GetDerivedPosition();
		CHECK(lanternPosition.x == Approx(112.0f).margin(0.001f));
		CHECK(lanternPosition.y == Approx(0.0f).margin(0.001f));
		CHECK(lanternPosition.z == Approx(0.0f).margin(0.001f));

		// Moving the town moves everything inside it
		node->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
		scene->GetRootSceneNode().Update(true, false);
		const Vector3 moved = lanternInstance->GetParentSceneNode()->GetDerivedPosition();
		CHECK(moved.x == Approx(12.0f).margin(0.001f));
		CHECK(moved.y == Approx(5.0f).margin(0.001f));

		// Visibility propagates down the chain
		instance.SetVisible(false);
		CHECK_FALSE(houseInstance->IsVisible());
		CHECK_FALSE(lanternInstance->IsVisible());

		instance.Destroy(*scene);
		CHECK(instance.GetChildInstanceCount() == 0);
		node->DetachObject(instance);
	}
}

TEST_CASE("World model instances refuse child references that recurse", "[world_model_nesting]")
{
	EnsureNullDevice();
	ManualModels models;

	const WorldModelPtr self = models.Create("test/nesting/self.hwmo");
	self->AddChildRef(MakeChildRef("test/nesting/self.hwmo", Vector3(1.0f, 0.0f, 0.0f)));

	const WorldModelPtr a = models.Create("test/nesting/a.hwmo");
	const WorldModelPtr b = models.Create("test/nesting/b.hwmo");
	a->AddChildRef(MakeChildRef("test/nesting/b.hwmo", Vector3::Zero));
	b->AddChildRef(MakeChildRef("test/nesting/a.hwmo", Vector3::Zero));

	auto scene = std::make_unique<OctreeScene>();
	{
		WorldModelInstance selfInstance("Self", self);
		SceneNode* selfNode = scene->GetRootSceneNode().CreateChildSceneNode("SelfNode");
		selfNode->AttachObject(selfInstance);
		CHECK(selfInstance.GetChildInstanceCount() == 0);

		// a -> b -> (a refused)
		WorldModelInstance aInstance("A", a);
		SceneNode* aNode = scene->GetRootSceneNode().CreateChildSceneNode("ANode");
		aNode->AttachObject(aInstance);
		REQUIRE(aInstance.GetChildInstanceCount() == 1);
		CHECK(aInstance.GetChildInstance(0)->GetChildInstanceCount() == 0);

		selfInstance.Destroy(*scene);
		aInstance.Destroy(*scene);
		selfNode->DetachObject(selfInstance);
		aNode->DetachObject(aInstance);
	}
}

TEST_CASE("Haven town places existing building world models", "[world_model_nesting]")
{
	// Haven_Town.hwmo is the town shell; every building and the keep are child world models, so a
	// renamed or deleted building file would silently leave a hole in the town.
	WorldModel town;
	if (!LoadShippedWorldModel("Models/Haven/Haven_Town.hwmo", town))
	{
		WARN("data/client is not checked out, skipping");
		return;
	}

	REQUIRE(town.GetGroupCount() > 0);
	REQUIRE(town.GetChildRefs().size() >= 20);

	for (const WorldModelChildRef& ref : town.GetChildRefs())
	{
		INFO("child " << ref.name << " -> " << ref.wmoPath);
		CHECK(ref.visible);
		CHECK(ref.wmoPath != "Models/Haven/Haven_Town.hwmo");

		WorldModel building;
		REQUIRE(LoadShippedWorldModel(ref.wmoPath, building));
		CHECK(building.GetGroupCount() > 0);
		CHECK(building.GetChildRefs().empty());

		for (size_t g = 0; g < building.GetGroupCount(); ++g)
		{
			for (const WorldModelMeshRef& meshRef : building.GetGroup(g)->GetMeshRefs())
			{
				INFO("mesh " << meshRef.meshPath);
				CHECK(std::filesystem::exists(MMO_SOURCE_DIR "/data/client/" + meshRef.meshPath));
			}
		}
	}
}
