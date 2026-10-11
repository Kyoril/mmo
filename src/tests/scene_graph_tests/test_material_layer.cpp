// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "null_device.h"

#include "graphics/material.h"
#include "graphics/material_instance.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_layer.h"
#include "scene_graph/mesh.h"
#include "scene_graph/sub_entity.h"
#include "scene_graph/sub_mesh.h"

using namespace mmo;
using mmo::test::EnsureNullDevice;

// Clothing painted in the body UV layout used to replace the body sub entity's material with a
// masked armor material, punching holes wherever the clothing texture is transparent (the feet
// below a pair of pants). Material layers composite the clothing onto slots of the body
// material instead, so whatever skin the character customization put there stays visible.

namespace
{
	/// A stand-in for the character body material: a skin texture plus one "Legs" layer slot.
	MaterialPtr MakeBodyMaterial(const String& name, const String& skin)
	{
		auto material = std::make_shared<Material>(name);
		material->AddTextureParameter("Skin", skin);
		material->AddTextureParameter("Legs_BaseColor", "Opacity.htex");
		material->AddScalarParameter("Legs_RoughnessIntensity", 1.0f);
		material->AddVectorParameter("Tint", Vector4(0.0f, 0.0f, 0.0f, 1.0f));
		return material;
	}

	/// A stand-in for an armor material instance (Pants_*.hmi).
	MaterialPtr MakePantsMaterial(const String& baseColor, const float roughness)
	{
		auto material = std::make_shared<Material>("Pants");
		material->AddTextureParameter("BaseColor", baseColor);
		material->AddScalarParameter("RoughnessIntensity", roughness);
		material->AddVectorParameter("Tint", Vector4(1.0f, 0.0f, 0.0f, 1.0f));
		return material;
	}

	MeshPtr MakeBodyMesh(const MaterialPtr& defaultMaterial)
	{
		auto mesh = std::make_shared<Mesh>("Body");
		mesh->CreateSubMesh("Naked_Legs").SetMaterial(defaultMaterial);
		mesh->CreateSubMesh("Naked_Feet").SetMaterial(defaultMaterial);
		return mesh;
	}

	String TextureParam(MaterialInterface& material, const std::string_view name)
	{
		String value;
		REQUIRE(material.GetTextureParameter(name, value));
		return value;
	}

	float ScalarParam(MaterialInterface& material, const std::string_view name)
	{
		float value = 0.0f;
		REQUIRE(material.GetScalarParameter(name, value));
		return value;
	}
}

TEST_CASE("A material layer fills the slot and keeps the customized skin", "[material_layer]")
{
	EnsureNullDevice();

	const MaterialPtr meshDefault = MakeBodyMaterial("Body", "DefaultSkin.htex");
	Entity entity("Character", MakeBodyMesh(meshDefault));

	// Character customization picked a different skin before the equipment was applied.
	const MaterialPtr customizedSkin = MakeBodyMaterial("DarkSkin", "DarkSkin.htex");
	entity.GetSubEntity("Naked_Legs")->SetMaterial(customizedSkin);

	REQUIRE(ApplyMaterialLayer(entity, "Naked_Legs", "Legs", MakePantsMaterial("Pants_D.htex", 0.25f)));

	const MaterialPtr result = entity.GetSubEntity("Naked_Legs")->GetMaterial();
	REQUIRE(result != customizedSkin);
	CHECK(TextureParam(*result, "Skin") == "DarkSkin.htex");
	CHECK(TextureParam(*result, "Legs_BaseColor") == "Pants_D.htex");
	CHECK(ScalarParam(*result, "Legs_RoughnessIntensity") == Approx(0.25f));

	// The armor's own "Tint" becomes "Legs_Tint", which the body does not declare: the body's
	// spell tint must stay untouched.
	Vector4 tint;
	REQUIRE(result->GetVectorParameter("Tint", tint));
	CHECK(tint.x == Approx(0.0f));

	// Other sub entities and the shared materials are not touched.
	CHECK(entity.GetSubEntity("Naked_Feet")->GetMaterial() == meshDefault);
	CHECK(TextureParam(*customizedSkin, "Legs_BaseColor") == "Opacity.htex");
}

TEST_CASE("Material layers on the same sub entity stack on one instance", "[material_layer]")
{
	EnsureNullDevice();

	auto body = std::make_shared<Material>("Body");
	body->AddTextureParameter("Legs_BaseColor", "Opacity.htex");
	body->AddTextureParameter("Hands_BaseColor", "Opacity.htex");
	Entity entity("Character", MakeBodyMesh(body));

	auto gloves = std::make_shared<Material>("Gloves");
	gloves->AddTextureParameter("BaseColor", "Gloves_D.htex");

	REQUIRE(ApplyMaterialLayer(entity, "Naked_Legs", "Legs", MakePantsMaterial("Pants_D.htex", 1.0f)));
	const MaterialPtr afterFirst = entity.GetSubEntity("Naked_Legs")->GetMaterial();

	REQUIRE(ApplyMaterialLayer(entity, "Naked_Legs", "Hands", gloves));
	const MaterialPtr afterSecond = entity.GetSubEntity("Naked_Legs")->GetMaterial();

	CHECK(afterSecond == afterFirst);
	CHECK(TextureParam(*afterSecond, "Legs_BaseColor") == "Pants_D.htex");
	CHECK(TextureParam(*afterSecond, "Hands_BaseColor") == "Gloves_D.htex");
}

TEST_CASE("Resetting sub entities drops every material layer", "[material_layer]")
{
	EnsureNullDevice();

	const MaterialPtr body = MakeBodyMaterial("Body", "Skin.htex");
	Entity entity("Character", MakeBodyMesh(body));

	REQUIRE(ApplyMaterialLayer(entity, "Naked_Legs", "Legs", MakePantsMaterial("Pants_D.htex", 1.0f)));
	entity.ResetSubEntities();

	CHECK(entity.GetSubEntity("Naked_Legs")->GetMaterial() == body);
}

TEST_CASE("A material layer on an unknown sub entity is ignored", "[material_layer]")
{
	EnsureNullDevice();

	Entity entity("Character", MakeBodyMesh(MakeBodyMaterial("Body", "Skin.htex")));

	CHECK_FALSE(ApplyMaterialLayer(entity, "Cape_01", "Legs", MakePantsMaterial("Pants_D.htex", 1.0f)));
	CHECK_FALSE(ApplyMaterialLayer(entity, "Naked_Legs", "Legs", nullptr));
}
