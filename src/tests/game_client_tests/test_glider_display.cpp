// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The glider's mesh and attachment come from the equipped glider's item display.

#include "catch.hpp"

#include "game_client/glider_display.h"
#include "shared/client_data/proto_client/item_display.pb.h"

using namespace mmo;

namespace
{
	proto_client::ItemDisplayEntry MakeDisplay(const uint32 model)
	{
		proto_client::ItemDisplayEntry display;
		display.set_id(240);
		display.set_name("Elowen's Prototype");
		auto* variant = display.add_variants();
		variant->set_model(model);
		variant->set_mesh("Models/Gear/Glider/Glider.hmsh");
		auto* bone = variant->mutable_attached_bone_default();
		bone->set_bone_name("Back_M");
		bone->set_offset_x(0.10f);
		bone->set_offset_y(0.05f);
		bone->set_rotation_w(0.70710678f);
		bone->set_rotation_y(0.70710678f);
		return display;
	}
}

TEST_CASE("a glider display resolves mesh, bone and transform", "[glider]")
{
	const auto attachment = ResolveGliderAttachment(MakeDisplay(0), 8, {});
	REQUIRE(attachment.has_value());
	CHECK(attachment->mesh == "Models/Gear/Glider/Glider.hmsh");
	CHECK(attachment->bone == "Back_M");
	CHECK(attachment->offset.x == Approx(0.10f));
	CHECK(attachment->offset.y == Approx(0.05f));
	CHECK(attachment->rotation.w == Approx(0.70710678f));
	CHECK(attachment->rotation.y == Approx(0.70710678f));
	CHECK(attachment->scale.x == Approx(1.0f));
}

TEST_CASE("a glider variant for another model is skipped", "[glider]")
{
	CHECK_FALSE(ResolveGliderAttachment(MakeDisplay(7), 8, {}).has_value());
	CHECK(ResolveGliderAttachment(MakeDisplay(8), 8, {}).has_value());
}

TEST_CASE("a glider display without mesh or bone resolves to nothing", "[glider]")
{
	auto noMesh = MakeDisplay(0);
	noMesh.mutable_variants(0)->clear_mesh();
	CHECK_FALSE(ResolveGliderAttachment(noMesh, 8, {}).has_value());

	auto noBone = MakeDisplay(0);
	noBone.mutable_variants(0)->clear_attached_bone_default();
	CHECK_FALSE(ResolveGliderAttachment(noBone, 8, {}).has_value());
}
