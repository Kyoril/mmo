// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/item_display_model_match.h"

#include <map>

using namespace mmo;

namespace
{
	// Mirrors the live model data: the player models are customizable avatars resolving to
	// their base mesh, while the guard NPC model references the same mesh directly.
	constexpr uint32 playerFemale = 7;
	constexpr uint32 playerMale = 8;
	constexpr uint32 guardMale = 24;
	constexpr uint32 unknownModel = 999;

	String ResolveMesh(const uint32 modelId)
	{
		static const std::map<uint32, String> meshes =
		{
			{ playerFemale, "Models/Character/Human/Female/HumanFemale_V2.hmsh" },
			{ playerMale, "Models/Character/Human/Male/HumanMale.hmsh" },
			{ guardMale, "Models/Character/Human/Male/HumanMale.hmsh" },
		};

		const auto it = meshes.find(modelId);
		return it == meshes.end() ? String() : it->second;
	}
}

TEST_CASE("item display variant for all models applies everywhere", "[item_display]")
{
	CHECK(ItemDisplayVariantAppliesToModel(0, playerMale, &ResolveMesh));
	CHECK(ItemDisplayVariantAppliesToModel(0, unknownModel, &ResolveMesh));
	CHECK(ItemDisplayVariantAppliesToModel(0, guardMale, ModelMeshResolver()));
}

TEST_CASE("item display variant applies to its own model", "[item_display]")
{
	CHECK(ItemDisplayVariantAppliesToModel(playerMale, playerMale, &ResolveMesh));
	CHECK(ItemDisplayVariantAppliesToModel(playerMale, playerMale, ModelMeshResolver()));
}

TEST_CASE("item display variant applies to NPC models sharing the mesh", "[item_display]")
{
	// A male-only variant authored against the player model dresses the male guard...
	CHECK(ItemDisplayVariantAppliesToModel(playerMale, guardMale, &ResolveMesh));

	// ...but the female variant of the same display must not.
	CHECK_FALSE(ItemDisplayVariantAppliesToModel(playerFemale, guardMale, &ResolveMesh));
}

TEST_CASE("item display variant without mesh resolution only matches exactly", "[item_display]")
{
	CHECK_FALSE(ItemDisplayVariantAppliesToModel(playerMale, guardMale, ModelMeshResolver()));

	// Two unresolvable models must not match each other through their empty mesh names.
	CHECK_FALSE(ItemDisplayVariantAppliesToModel(unknownModel, unknownModel + 1, &ResolveMesh));
}
