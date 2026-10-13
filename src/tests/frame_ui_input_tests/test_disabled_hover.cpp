// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "frame_ui/frame.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/localization.h"
#include "graphics/graphics_device.h"

#include "lua.hpp"

using namespace mmo;

namespace
{
	/// Frame which records the input events it receives, without needing a window or layout manager.
	class RecordingFrame final : public Frame
	{
	public:
		explicit RecordingFrame(const std::string& name)
			: Frame("Frame", name)
		{
		}

		/// Supplies already-computed screen bounds.
		void SetTestBounds(const Rect& rect)
		{
			m_absRectCache = rect;
			m_needsLayout = false;
		}

		void OnMouseEnter() override
		{
			Frame::OnMouseEnter();
			++enterCount;
		}

		void OnMouseLeave() override
		{
			Frame::OnMouseLeave();
			++leaveCount;
		}

		bool OnMouseDown(MouseButton button, int32 buttons, const Point& position) override
		{
			++mouseDownCount;
			return Frame::OnMouseDown(button, buttons, position);
		}

		bool OnMouseWheel(int32 delta) override
		{
			++wheelCount;
			return wheelConsumes;
		}

		int enterCount = 0;
		int leaveCount = 0;
		int mouseDownCount = 0;
		int wheelCount = 0;
		bool wheelConsumes = false;
	};

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
}

TEST_CASE("Disabled frames take part in hover tracking", "[frame_input][disabled_hover]")
{
	FrameManagerFixture fixture;

	auto root = std::make_shared<RecordingFrame>("Root");
	auto panel = std::make_shared<RecordingFrame>("Panel");
	auto button = std::make_shared<RecordingFrame>("LockedButton");
	root->AddChild(panel);
	panel->AddChild(button);
	root->SetTestBounds(Rect(0, 0, 200, 200));
	panel->SetTestBounds(Rect(0, 0, 100, 100));
	button->SetTestBounds(Rect(10, 10, 50, 50));
	button->SetEnabled(false);

	FrameManager::Get().SetTopFrame(root);

	SECTION("Hover enter and leave are delivered")
	{
		FrameManager::Get().NotifyMouseMoved(Point(20, 20));
		CHECK(FrameManager::Get().GetHoveredFrame() == button);
		CHECK(button->IsHovered());
		CHECK(button->enterCount == 1);

		FrameManager::Get().NotifyMouseMoved(Point(80, 80));
		CHECK_FALSE(button->IsHovered());
		CHECK(button->leaveCount == 1);
	}

	SECTION("Mouse wheel never reaches the disabled frame and bubbles from the enabled ancestor")
	{
		panel->wheelConsumes = true;
		FrameManager::Get().NotifyMouseMoved(Point(20, 20));

		CHECK(FrameManager::Get().NotifyMouseWheel(1));
		CHECK(button->wheelCount == 0);
		CHECK(panel->wheelCount == 1);
	}

	SECTION("Clicks on a disabled frame do not fire")
	{
		FrameManager::Get().NotifyMouseMoved(Point(20, 20));
		FrameManager::Get().NotifyMouseDown(MouseButton::Left, Point(20, 20));
		FrameManager::Get().NotifyMouseUp(MouseButton::Left, Point(20, 20));
		CHECK(button->mouseDownCount == 0);
	}

	SECTION("Enabled frames still hover normally")
	{
		button->SetEnabled(true);
		FrameManager::Get().NotifyMouseMoved(Point(20, 20));
		CHECK(button->IsHovered());
		CHECK(button->enterCount == 1);
	}
}
