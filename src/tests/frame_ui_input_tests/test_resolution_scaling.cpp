// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "frame_ui/frame_mgr.h"
#include "lua.hpp"

using namespace mmo;

TEST_CASE("Resolution changes preserve UI proportions and refresh cached layout", "[frame_ui][resolution]")
{
	if (!GraphicsDevice::HasInstance())
	{
		GraphicsDevice::CreateNull(GraphicsDeviceDesc());
	}
	Localization localization;
	auto* lua = luaL_newstate();
	luaL_openlibs(lua);
	luabind::open(lua);
	FrameManager::Initialize(lua, localization);
	{
		auto& manager = FrameManager::Get();
		auto root = manager.Create("Frame", "ResolutionRoot");
		auto socket = manager.Create("Frame", "ResolutionSocket");
		root->AddChild(socket);
		manager.SetTopFrame(root);
		socket->SetSize(112, 112);
		manager.NotifyScreenSizeChanged(1920, 1080);
		CHECK(socket->GetAbsoluteFrameRect().GetWidth() == Approx(56));
		manager.NotifyScreenSizeChanged(1280, 1024);
		CHECK(manager.GetUIScale().x == Approx(manager.GetUIScale().y));
		const auto bounds = socket->GetAbsoluteFrameRect();
		CHECK(bounds.GetWidth() == Approx(112.0f / 3));
		CHECK(bounds.GetHeight() == Approx(bounds.GetWidth()));
		CHECK(manager.GetTextScale() == Approx(manager.GetUIScale().x));

		manager.SetMinimumUIScale(0.5f, Size(2560, 1600));
		for (const auto& size : { Size(1024, 768), Size(1280, 720), Size(1280, 960), Size(1280, 1024), Size(1920, 1080) })
		{
			manager.NotifyScreenSizeChanged(size.width, size.height);
			const float scale = manager.GetUIScale().x;
			CHECK(scale == Approx(manager.GetUIScale().y));
			CHECK(scale >= 0.4f);
			CHECK(2200 * scale <= size.width); // Options panel.
			CHECK(1580 * scale <= size.height); // Character panel plus top offset.
			const auto canvas = manager.GetLogicalScreenSize();
			CHECK(canvas.width * scale == Approx(size.width));
			CHECK(canvas.height * scale == Approx(size.height));
			CHECK(socket->GetAbsoluteFrameRect().GetWidth() == Approx(112 * scale));
		}
		manager.NotifyScreenSizeChanged(1280, 720);
		CHECK(manager.GetUIScale().x == Approx(0.45f));
		manager.NotifyScreenSizeChanged(0, 0);
		CHECK(manager.GetUIScale().x == Approx(0.45f));
	}
	FrameManager::Get().ResetTopFrame();
	FrameManager::Destroy();
	lua_close(lua);
}
