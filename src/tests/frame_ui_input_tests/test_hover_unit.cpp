// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "frame_ui/frame.h"
#include "graphics/graphics_device.h"

using namespace mmo;

TEST_CASE("Unit frames report the unit that hovering them stands in for", "[frame_input][hover_unit]")
{
	// Frames need a graphics device; other cases in this suite may already have created it.
	if (!GraphicsDevice::HasInstance())
	{
		GraphicsDevice::CreateNull(GraphicsDeviceDesc());
	}

	auto playerFrame = std::make_shared<Frame>("Frame", "PlayerFrame");
	auto clickFrame = std::make_shared<Frame>("Frame", "PlayerFrameClick");
	playerFrame->AddChild(clickFrame);

	// Ordinary frames do not represent a unit.
	CHECK(clickFrame->GetHoverUnit().empty());
	CHECK(playerFrame->GetHoverUnit().empty());

	// Unit frames opt in through the HoverUnit property set in their layout XML.
	clickFrame->AddProperty("HoverUnit", "player");
	CHECK(clickFrame->GetHoverUnit() == "player");

	// Only the hovered frame itself counts: children such as aura buttons keep their own tooltips.
	CHECK(playerFrame->GetHoverUnit().empty());

	clickFrame->SetProperty("HoverUnit", "target");
	CHECK(clickFrame->GetHoverUnit() == "target");
}
