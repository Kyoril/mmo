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
	/// Frame which records the hover events it receives, without needing a window or layout manager.
	class HoverFrame final : public Frame
	{
	public:
		explicit HoverFrame(const std::string& name)
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

		int enterCount = 0;
		int leaveCount = 0;
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

// The inventory rebuilds every slot button whenever the player's equipment changes
// (InventoryFrame_UpdateSlots clears ItemSlotContainer and clones fresh buttons), so right
// clicking an item to equip it destroys the very button the cursor rests on. Hover used to be
// resolved on mouse movement alone, so the manager kept the destroyed button as the hovered
// frame: its tooltip was never hidden and the replacement never got its OnEnter, leaving the
// previous item's tooltip on screen until the player moved the mouse.
TEST_CASE("A frame replaced under a resting cursor takes over the hover", "[frame_input][hover_replaced]")
{
	FrameManagerFixture fixture;

	auto root = std::make_shared<HoverFrame>("Root");
	auto container = std::make_shared<HoverFrame>("SlotContainer");
	auto slot = std::make_shared<HoverFrame>("Slot");
	root->AddChild(container);
	container->AddChild(slot);
	root->SetTestBounds(Rect(0, 0, 200, 200));
	container->SetTestBounds(Rect(0, 0, 100, 100));
	slot->SetTestBounds(Rect(10, 10, 50, 50));

	FrameManager::Get().SetTopFrame(root);

	FrameManager::Get().NotifyMouseMoved(Point(20, 20));
	REQUIRE(FrameManager::Get().GetHoveredFrame() == slot);
	REQUIRE(slot->enterCount == 1);

	SECTION("The identically placed replacement is entered without mouse movement")
	{
		container->RemoveAllChildren();

		auto replacement = std::make_shared<HoverFrame>("SlotReplacement");
		container->AddChild(replacement);
		// Removing the children invalidated the container's layout; the real UI recomputes it
		// from the anchors, the test simply restores the known bounds.
		container->SetTestBounds(Rect(0, 0, 100, 100));
		replacement->SetTestBounds(Rect(10, 10, 50, 50));

		FrameManager::Get().Update(0.016f);

		CHECK(slot->leaveCount == 1);
		CHECK_FALSE(slot->IsHovered());
		CHECK(FrameManager::Get().GetHoveredFrame() == replacement);
		CHECK(replacement->enterCount == 1);
		CHECK(replacement->IsHovered());
	}

	SECTION("An emptied container hands the hover back to itself")
	{
		container->RemoveAllChildren();
		container->SetTestBounds(Rect(0, 0, 100, 100));

		FrameManager::Get().Update(0.016f);

		CHECK(slot->leaveCount == 1);
		CHECK(FrameManager::Get().GetHoveredFrame() == container);
		CHECK(container->enterCount == 1);
	}

	SECTION("An unchanged hierarchy neither re-enters nor leaves the hovered frame")
	{
		FrameManager::Get().Update(0.016f);
		FrameManager::Get().Update(0.016f);

		CHECK(slot->enterCount == 1);
		CHECK(slot->leaveCount == 0);
		CHECK(FrameManager::Get().GetHoveredFrame() == slot);
	}
}
