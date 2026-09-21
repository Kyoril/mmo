// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "frame_ui/frame.h"
#include "graphics/graphics_device.h"

using namespace mmo;

namespace
{
	class HitTestFrame final : public Frame
	{
	public:
		/// Creates a frame without a renderer for input tests.
		explicit HitTestFrame(const std::string& name)
			: Frame("Frame", name)
		{
		}

		/// Supplies already-computed screen bounds without a window or layout manager.
		void SetTestBounds()
		{
			m_absRectCache = Rect(0, 0, 100, 100);
			m_needsLayout = false;
		}
	};
}

TEST_CASE("Decorative overlays pass mouse hit tests to the window underneath", "[frame_input]")
{
	static auto& device = GraphicsDevice::CreateNull(GraphicsDeviceDesc());
	(void)device;

	auto root = std::make_shared<HitTestFrame>("Root");
	auto tab = std::make_shared<HitTestFrame>("ClassesTab");
	auto overlay = std::make_shared<HitTestFrame>("ErrorFrame");
	auto text = std::make_shared<HitTestFrame>("ErrorText");
	root->AddChild(tab);
	root->AddChild(overlay);

	SECTION("Empty overlay")
	{
	}
	SECTION("Overlay displaying a message")
	{
		overlay->AddChild(text);
	}

	root->SetTestBounds();
	tab->SetTestBounds();
	overlay->SetTestBounds();
	text->SetTestBounds();
	const Point cursor(50, 50);

	// Reproduce the blocker before opting into pass-through.
	CHECK(root->GetChildFrameAt(cursor, false) != tab);
	CHECK(root->GetChildFrameAt(cursor, true) != tab);
	overlay->GetProperty("MousePassThrough")->Set("true");
	CHECK(root->GetChildFrameAt(cursor, false) == tab); // Hover / wheel target.
	CHECK(root->GetChildFrameAt(cursor, true) == tab); // Mouse-down target.
	CHECK(overlay->IsVisible());

	// Restoring ordinary behavior must continue to block clicks behind interactive frames.
	overlay->GetProperty("MousePassThrough")->Set("false");
	CHECK(root->GetChildFrameAt(cursor, true) != tab);
}
