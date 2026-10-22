// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/vector3.h"
#include "scene_graph/prop_kit_manager.h"
#include "scene_graph/world_model_flatten.h"
#include "scene_graph/world_model_manager.h"

#include <algorithm>

using namespace mmo;

namespace
{
	/// Registers a manual world model under a name and removes it again when the test ends, so test
	/// cases never see each other's models in the manager's cache.
	struct ScopedWorldModel
	{
		explicit ScopedWorldModel(const String& name)
			: name(name)
			, model(WorldModelManager::Get().CreateManual(name))
		{
		}

		~ScopedWorldModel()
		{
			WorldModelManager::Get().Remove(name);
		}

		String name;
		WorldModelPtr model;
	};

	WorldModelGroup& AddGroup(WorldModel& model, const uint32 flags, const std::vector<String>& meshPaths)
	{
		WorldModelGroup& group = model.AddGroup();
		group.SetFlags(flags);
		for (const String& path : meshPaths)
		{
			WorldModelMeshRef ref;
			ref.meshPath = path;
			group.AddMeshRef(ref);
		}
		return group;
	}

	const FlattenedMeshRef* FindRef(const std::vector<FlattenedMeshRef>& refs, const String& path)
	{
		const auto it = std::find_if(refs.begin(), refs.end(), [&path](const FlattenedMeshRef& ref) { return ref.meshPath == path; });
		return it == refs.end() ? nullptr : &*it;
	}

	bool Near(const Vector3& a, const Vector3& b)
	{
		return (a - b).GetLength() < 0.001f;
	}
}

TEST_CASE("Flatten skips pure interior groups and keeps exterior and mixed ones", "[world_model_flatten]")
{
	ScopedWorldModel wmo("FlattenTest/Interior.hwmo");
	AddGroup(*wmo.model, WorldModelGroupFlags::Exterior, { "Outside.hmsh" });
	AddGroup(*wmo.model, WorldModelGroupFlags::Interior, { "Inside_A.hmsh", "Inside_B.hmsh" });
	AddGroup(*wmo.model, WorldModelGroupFlags::Interior | WorldModelGroupFlags::Exterior, { "Porch.hmsh" });

	FlattenReport report;
	const std::vector<FlattenedMeshRef> refs = FlattenWorldModelExterior(wmo.model, Matrix4::Identity, FlattenOptions{}, &report);

	REQUIRE(refs.size() == 2);
	CHECK(FindRef(refs, "Outside.hmsh"));
	CHECK(FindRef(refs, "Porch.hmsh"));
	CHECK(report.skippedInteriorGroups == 1);
	CHECK(report.skippedInteriorMeshes == 2);

	FlattenOptions keepAll;
	keepAll.skipInterior = false;
	CHECK(FlattenWorldModelExterior(wmo.model, Matrix4::Identity, keepAll).size() == 4);
}

TEST_CASE("Flatten composes child world model transforms", "[world_model_flatten]")
{
	ScopedWorldModel child("FlattenTest/Child.hwmo");
	WorldModelGroup& childGroup = AddGroup(*child.model, WorldModelGroupFlags::Exterior, {});
	WorldModelMeshRef childMesh;
	childMesh.meshPath = "ChildMesh.hmsh";
	childMesh.position = Vector3(1.0f, 0.0f, 0.0f);
	childGroup.AddMeshRef(childMesh);

	ScopedWorldModel parent("FlattenTest/Parent.hwmo");
	AddGroup(*parent.model, WorldModelGroupFlags::Exterior, { "ParentMesh.hmsh" });
	WorldModelChildRef childRef;
	childRef.wmoPath = child.name;
	childRef.position = Vector3(0.0f, 0.0f, 5.0f);
	childRef.scale = Vector3(2.0f, 2.0f, 2.0f);
	parent.model->AddChildRef(childRef);

	WorldModelChildRef hidden = childRef;
	hidden.visible = false;
	parent.model->AddChildRef(hidden);

	Matrix4 placement;
	placement.MakeTransform(Vector3(10.0f, 0.0f, 0.0f), Vector3::UnitScale, Quaternion::Identity);

	FlattenReport report;
	const std::vector<FlattenedMeshRef> refs = FlattenWorldModelExterior(parent.model, placement, FlattenOptions{}, &report);

	REQUIRE(refs.size() == 2);
	const FlattenedMeshRef* childRefOut = FindRef(refs, "ChildMesh.hmsh");
	REQUIRE(childRefOut);

	// Placement (10,0,0) + child offset (0,0,5) + child-scaled mesh offset 2 * (1,0,0)
	CHECK(Near(childRefOut->world * Vector3::Zero, Vector3(12.0f, 0.0f, 5.0f)));
	CHECK(report.worldModelPaths.size() == 1);
}

TEST_CASE("Flatten stops at a world model nesting itself", "[world_model_flatten]")
{
	ScopedWorldModel wmo("FlattenTest/Recursive.hwmo");
	AddGroup(*wmo.model, WorldModelGroupFlags::Exterior, { "Wall.hmsh" });
	WorldModelChildRef self;
	self.wmoPath = wmo.name;
	wmo.model->AddChildRef(self);

	FlattenReport report;
	const std::vector<FlattenedMeshRef> refs = FlattenWorldModelExterior(wmo.model, Matrix4::Identity, FlattenOptions{}, &report);

	CHECK(refs.size() == 1);
	CHECK(report.skippedChildren == 1);
}

TEST_CASE("Flatten places the global and the active doodad set, and the active one on the top level only", "[world_model_flatten]")
{
	const auto addDoodads = [](WorldModel& model)
	{
		model.GetDoodadNames() = { "Global.hmsh", "SetOne.hmsh", "SetTwo.hmsh" };
		for (uint32 i = 0; i < 3; ++i)
		{
			WorldModelDoodad doodad{};
			doodad.nameIndex = i;
			doodad.rotation = Quaternion::Identity;
			doodad.scale = 1.0f;
			model.GetDoodads().push_back(doodad);
		}
		model.GetDoodadSets() = { { "Global", 0, 1 }, { "One", 1, 1 }, { "Two", 2, 1 } };
	};

	ScopedWorldModel child("FlattenTest/DoodadChild.hwmo");
	addDoodads(*child.model);

	ScopedWorldModel wmo("FlattenTest/Doodads.hwmo");
	addDoodads(*wmo.model);
	WorldModelChildRef childRef;
	childRef.wmoPath = child.name;
	wmo.model->AddChildRef(childRef);

	FlattenOptions options;
	options.doodadSet = 1;
	const std::vector<FlattenedMeshRef> refs = FlattenWorldModelExterior(wmo.model, Matrix4::Identity, options);

	const auto count = [&refs](const String& path)
	{
		return std::count_if(refs.begin(), refs.end(), [&path](const FlattenedMeshRef& ref) { return ref.meshPath == path; });
	};

	CHECK(count("Global.hmsh") == 2);
	CHECK(count("SetOne.hmsh") == 1);
	CHECK(count("SetTwo.hmsh") == 0);

	options.includeDoodads = false;
	CHECK(FlattenWorldModelExterior(wmo.model, Matrix4::Identity, options).empty());
}

TEST_CASE("Flatten dresses the top level with prop kits and places their lights in world space", "[world_model_flatten]")
{
	const String kitPath = "FlattenTest/Kit.hkit";
	auto kit = std::make_shared<PropKit>();

	WorldModelMeshRef prop;
	prop.meshPath = "Barrel.hmsh";
	prop.position = Vector3(0.0f, 1.0f, 0.0f);
	prop.materialOverride = "Models/Wood.hmi";
	kit->props.push_back(prop);

	WorldModelMeshRef torch;
	torch.meshPath = "Effects/Fire.hpar";
	kit->props.push_back(torch);

	WorldModelLight light{};
	light.type = WorldModelLight::LightType::Omni;
	light.position = Vector3(0.0f, 3.0f, 0.0f);
	light.rotation = Quaternion::Identity;
	light.attenuationEnd = 8.0f;
	kit->lights.push_back(light);

	PropKitManager::Get().Insert(kitPath, kit);

	ScopedWorldModel wmo("FlattenTest/Kitted.hwmo");
	AddGroup(*wmo.model, WorldModelGroupFlags::Exterior, {});

	Matrix4 placement;
	placement.MakeTransform(Vector3(0.0f, 0.0f, 20.0f), Vector3::UnitScale, Quaternion::Identity);

	FlattenOptions options;
	options.propKitPaths = { kitPath };

	FlattenReport report;
	const std::vector<FlattenedMeshRef> refs = FlattenWorldModelExterior(wmo.model, placement, options, &report);

	// The particle system is not a mesh
	REQUIRE(refs.size() == 1);
	CHECK(refs[0].meshPath == "Barrel.hmsh");
	REQUIRE(refs[0].materialOverrides.size() == 1);
	CHECK(refs[0].materialOverrides[0] == "Models/Wood.hmi");
	CHECK(Near(refs[0].world * Vector3::Zero, Vector3(0.0f, 1.0f, 20.0f)));
	CHECK(report.propKitPaths == std::vector<String>{ kitPath });

	options.includePropKits = false;
	CHECK(FlattenWorldModelExterior(wmo.model, placement, options).empty());
	options.includePropKits = true;

	// Extras: the kit light and the particle system, both in world space. This kit holds no mesh,
	// so the extras walk has nothing to load for sound emitters.
	ScopedWorldModel lightsOnly("FlattenTest/KittedLights.hwmo");
	AddGroup(*lightsOnly.model, WorldModelGroupFlags::Exterior, {});

	auto lightKit = std::make_shared<PropKit>();
	lightKit->props.push_back(torch);
	lightKit->lights.push_back(light);
	const String lightKitPath = "FlattenTest/LightKit.hkit";
	PropKitManager::Get().Insert(lightKitPath, lightKit);

	FlattenOptions lightOptions;
	lightOptions.propKitPaths = { lightKitPath };
	const FlattenedWorldModelExtras extras = FlattenWorldModelExtras(lightsOnly.model, placement, lightOptions);

	REQUIRE(extras.lights.size() == 1);
	CHECK(Near(extras.lights[0].position, Vector3(0.0f, 3.0f, 20.0f)));
	CHECK(extras.lights[0].attenuationEnd == 8.0f);
	REQUIRE(extras.particles.size() == 1);
	CHECK(extras.particles[0].path == "Effects/Fire.hpar");
	CHECK(Near(extras.particles[0].world * Vector3::Zero, Vector3(0.0f, 0.0f, 20.0f)));
	CHECK(extras.sounds.empty());
}

TEST_CASE("Particle system paths are recognized case-insensitively", "[world_model_flatten]")
{
	CHECK(IsParticleSystemPath("Effects/Fire.hpar"));
	CHECK(IsParticleSystemPath("Effects/Fire.HPAR"));
	CHECK_FALSE(IsParticleSystemPath("Models/Fire.hmsh"));
	CHECK_FALSE(IsParticleSystemPath("par"));
}
