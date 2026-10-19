// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "base/signal.h"
#include "frame_ui/frame.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/localization.h"
#include "graphics/graphics_device.h"

#include "lua.hpp"

using namespace mmo;

namespace
{
	/// Plain ui frame with ready made screen bounds, so no window or layout manager is needed.
	class PanelFrame
		: public Frame
	{
	public:
		explicit PanelFrame(const std::string& name)
			: Frame("Frame", name)
		{
		}

		/// Supplies already-computed screen bounds.
		void SetTestBounds(const Rect& rect)
		{
			m_absRectCache = rect;
			m_needsLayout = false;
		}
	};

	/// Stand-in for the frame which renders the 3d world. It declines mouse presses so they reach
	/// the player controller, exactly like WorldFrame_OnMouseDown in GameParent.lua does.
	class WorldLikeFrame final
		: public PanelFrame
	{
	public:
		explicit WorldLikeFrame(const std::string& name)
			: PanelFrame(name)
		{
		}

		bool OnMouseDown(MouseButton button, int32 buttons, const Point& position) override
		{
			return false;
		}

		bool OnMouseUp(MouseButton button, int32 buttons, const Point& position) override
		{
			return false;
		}
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

	/// Mimics the client's mouse routing: the ui handler is connected first, and the game world
	/// handler only runs afterwards if the ui did not abort the emission.
	struct InputRouter
	{
		InputRouter()
		{
			connections += mouseDown.connect([](int32 x, int32 y)
				{
					return FrameManager::Get().NotifyMouseDown(MouseButton::Left, Point(x, y));
				}, true);
			connections += mouseUp.connect([](int32 x, int32 y)
				{
					return FrameManager::Get().NotifyMouseUp(MouseButton::Left, Point(x, y));
				}, true);

			connections += mouseDown.connect([this](int32, int32)
				{
					worldSawPress = true;
					return true;
				});
			connections += mouseUp.connect([this](int32, int32)
				{
					worldSawRelease = true;
					return true;
				});
		}

		/// Presses and releases the left mouse button at the given position.
		void Click(int32 x, int32 y)
		{
			FrameManager::Get().NotifyMouseMoved(Point(x, y));
			mouseDown(x, y);
			mouseUp(x, y);
		}

		signal<bool(int32, int32)> mouseDown;
		signal<bool(int32, int32)> mouseUp;
		bool worldSawPress = false;
		bool worldSawRelease = false;
		scoped_connection_container connections;
	};
}

TEST_CASE("A click which lands on the ui never acts on the game world as well", "[frame_input][ui_click]")
{
	FrameManagerFixture fixture;

	// The world frame covers the whole screen, the action bar sits on top of it at the bottom.
	auto root = std::make_shared<PanelFrame>("TopGameFrame");
	auto world = std::make_shared<WorldLikeFrame>("WorldFrame");
	auto actionBar = std::make_shared<PanelFrame>("ActionBar");
	root->AddChild(world);
	root->AddChild(actionBar);
	root->SetTestBounds(Rect(0, 0, 200, 200));
	world->SetTestBounds(Rect(0, 0, 200, 200));
	actionBar->SetTestBounds(Rect(0, 180, 200, 200));

	FrameManager::Get().SetTopFrame(root);

	InputRouter router;

	SECTION("A click on a bar without a click handler of its own stays in the ui")
	{
		// The gap between two action bar buttons belongs to the bar. Letting such a click reach
		// the player controller would clear the player's target.
		router.Click(100, 190);
		CHECK_FALSE(router.worldSawPress);
		CHECK_FALSE(router.worldSawRelease);
	}

	SECTION("A click on a child without a click handler stays in the ui as well")
	{
		auto label = std::make_shared<PanelFrame>("ActionBarLabel");
		actionBar->AddChild(label);
		label->SetTestBounds(Rect(20, 185, 60, 195));

		router.Click(40, 190);
		CHECK_FALSE(router.worldSawPress);
		CHECK_FALSE(router.worldSawRelease);
	}

	SECTION("A click into the world still reaches the player controller")
	{
		router.Click(100, 50);
		CHECK(router.worldSawPress);
		CHECK(router.worldSawRelease);
	}

	SECTION("A press started in the world keeps acting on the world when released over the ui")
	{
		FrameManager::Get().NotifyMouseMoved(Point(100, 50));
		router.mouseDown(100, 50);
		FrameManager::Get().NotifyMouseMoved(Point(100, 190));
		router.mouseUp(100, 190);

		CHECK(router.worldSawPress);
		CHECK(router.worldSawRelease);
	}
}
