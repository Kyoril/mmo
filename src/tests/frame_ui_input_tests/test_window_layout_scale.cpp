// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/image_component.h"
#include "lua.hpp"

using namespace mmo;

TEST_CASE("Compact windows preserve text size and align nested artwork and input", "[frame_ui][window_scale]")
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
		manager.SetMinimumUIScale(0.5f, Size(2560, 1600));
		manager.NotifyScreenSizeChanged(1280, 720);
		auto root = manager.Create("Frame", "CompactRoot");
		root->SetSize(2800, 1600);
		manager.SetTopFrame(root);
		auto window = manager.Create("Frame", "CompactWindow");
		root->AddChild(window);
		window->SetSize(1440, 1280);
		window->SetPosition(Point(100, 100));
		auto slot = manager.Create("Button", "CompactSlot");
		window->AddChild(slot);
		slot->SetSize(112, 112);
		slot->SetPosition(Point(32, 144));
		auto close = manager.Create("Button", "CompactClose");
		window->AddChild(close);
		close->SetSize(64, 64);
		close->SetAnchor(AnchorPoint::Right, AnchorPoint::Right, window, -32);
		close->SetAnchor(AnchorPoint::Top, AnchorPoint::Top, window, 24);
		// Populate the caches before resizing, as an already open window does.
		CHECK(slot->GetAbsoluteFrameRect().GetWidth() == Approx(50.4f));
		window->SetLayoutScale(0.85f);
		const auto bounds = window->GetAbsoluteFrameRect();
		CHECK(bounds.left == Approx(45));
		CHECK(bounds.top == Approx(45));
		CHECK(bounds.GetWidth() == Approx(550.8f));
		CHECK(bounds.GetHeight() == Approx(489.6f));
		CHECK(manager.GetTextScale() == Approx(0.45f));
		const auto socket = slot->GetAbsoluteFrameRect();
		CHECK(socket.left == Approx(45 + 32 * 0.3825f));
		CHECK(socket.top == Approx(45 + 144 * 0.3825f));
		CHECK(socket.GetWidth() == Approx(42.84f));
		CHECK(socket.GetHeight() == Approx(socket.GetWidth()));
		const auto button = close->GetAbsoluteFrameRect();
		CHECK(button.right == Approx(bounds.right - 32 * 0.3825f));
		CHECK(button.top == Approx(bounds.top + 24 * 0.3825f));
		CHECK(button.GetWidth() == Approx(button.GetHeight()));
		manager.NotifyMouseMoved(Point(socket.left + 10, socket.top + 10));
		CHECK(manager.GetHoveredFrame() == slot);
		ImageComponent art(*slot, "");
		art.SetInset(Rect(16, 16, 16, 16));
		CHECK(art.GetArea(socket).left == Approx(socket.left + 16 * 0.3825f));
		window->SetLayoutScale(1.0f);
		CHECK(slot->GetAbsoluteFrameRect().GetWidth() == Approx(50.4f));
		CHECK(manager.GetTextScale() == Approx(0.45f));
		window->SetLayoutScale(0.0f);
		CHECK(window->GetLayoutScale() == 1.0f);
	}
	FrameManager::Get().ResetTopFrame();
	FrameManager::Destroy();
	lua_close(lua);
}
