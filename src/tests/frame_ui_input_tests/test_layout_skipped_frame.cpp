// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "frame_ui/frame.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/layout_xml_loader.h"
#include "frame_ui/localization.h"
#include "graphics/graphics_device.h"
#include "xml_handler/xml_attributes.h"

#include "lua.hpp"

using namespace mmo;

namespace
{
	/// Owns a Lua state and a FrameManager for the duration of a test.
	struct FrameManagerFixture
	{
		FrameManagerFixture()
		{
			if (!GraphicsDevice::HasInstance())
			{
				GraphicsDevice::CreateNull(GraphicsDeviceDesc());
			}

			lua = luaL_newstate();
			luaL_openlibs(lua);
			luabind::open(lua);
			FrameManager::Initialize(lua, localization);
		}

		~FrameManagerFixture()
		{
			FrameManager::Get().ResetTopFrame();
			FrameManager::Destroy();
			lua_close(lua);
		}

		lua_State* lua = nullptr;
		Localization localization;
	};

	XmlAttributes FrameAttributes(const std::string& name)
	{
		XmlAttributes attributes;
		attributes.Add("name", name);
		return attributes;
	}

	XmlAttributes PropertyAttributes(const std::string& name, const std::string& value)
	{
		XmlAttributes attributes;
		attributes.Add("name", name);
		attributes.Add("value", value);
		return attributes;
	}
}

TEST_CASE("Content of a frame that failed to create is not applied to its parent", "[frame_ui][layout_xml]")
{
	FrameManagerFixture fixture;

	// Frame names are global: a second frame named "Cooldown" cannot be created.
	REQUIRE(FrameManager::Get().Create("Frame", "Cooldown") != nullptr);

	LayoutXmlLoader loader;
	loader.SetFilename("skipped-frame-test.xml");

	loader.ElementStart("Frame", FrameAttributes("ButtonTemplate"));
	loader.ElementStart("Frame", FrameAttributes("Cooldown"));
	loader.ElementStart("Property", PropertyAttributes("Visible", "false"));
	loader.ElementEnd("Property");
	loader.ElementStart("Frame", FrameAttributes("NestedInSkipped"));
	loader.ElementEnd("Frame");
	loader.ElementEnd("Frame");

	// The frame stack is intact again after the skipped subtree.
	loader.ElementStart("Property", PropertyAttributes("Clickable", "false"));
	loader.ElementEnd("Property");
	loader.ElementEnd("Frame");

	const FramePtr button = FrameManager::Get().Find("ButtonTemplate");
	REQUIRE(button != nullptr);
	CHECK(button->IsVisible());
	CHECK_FALSE(button->IsClickable());
	CHECK(button->GetChildCount() == 0);
	CHECK(FrameManager::Get().Find("NestedInSkipped") == nullptr);
}
