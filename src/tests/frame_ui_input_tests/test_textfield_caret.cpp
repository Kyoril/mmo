// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "assets/asset_registry.h"
#include "frame_ui/font_mgr.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/localization.h"
#include "frame_ui/textfield.h"
#include "graphics/graphics_device.h"

#include "lua.hpp"

using namespace mmo;

namespace
{
	/// Owns a Lua state, a FrameManager running at 1080p (UI scale 0.5 against the 4K native
	/// resolution) and the client data directory as asset root, so a real font can be loaded.
	struct CaretFixture
	{
		CaretFixture()
		{
			if (!GraphicsDevice::HasInstance())
			{
				GraphicsDevice::CreateNull(GraphicsDeviceDesc());
			}

			AssetRegistry::Initialize(MMO_TEST_CLIENT_DATA_DIR, {});

			lua = luaL_newstate();
			luaL_openlibs(lua);
			luabind::open(lua);
			FrameManager::Initialize(lua, localization);
			FrameManager::Get().NotifyScreenSizeChanged(1920.0f, 1080.0f);
			FrameManager::Get().AddFontMap("CaretTestFont", { "Fonts/EBGaramond-Regular.ttf", 32.0f, 0.0f, 0.0f, 0.0f });
		}

		~CaretFixture()
		{
			FrameManager::Get().SetCaptureWindow(nullptr);
			FrameManager::Get().ResetTopFrame();
			FrameManager::Destroy();
			lua_close(lua);
			AssetRegistry::Destroy();
		}

		lua_State* lua = nullptr;
		Localization localization;
	};

	/// Pen advance of an ASCII string in pixels: where Font::DrawText puts the next glyph, and so
	/// where the caret belongs. Font::GetTextWidth measures the last glyph's rendered extent instead.
	float PenAdvance(Font& font, const std::string& text, const float scale)
	{
		float x = 0.0f;
		for (const char c : text)
		{
			const FontGlyph* glyph = font.GetGlyphData(static_cast<uint32>(c));
			REQUIRE(glyph != nullptr);
			x += glyph->GetAdvance(scale);
		}
		return x;
	}
}

TEST_CASE("The caret sits at the end of the text with a scaled text area offset", "[frame_input][textfield_caret]")
{
	CaretFixture fixture;
	REQUIRE(FrameManager::Get().GetUIScaleSize().height == Approx(0.5f));

	// The glue fields: 600x128 UI units with the 34/36 insets GlueTextField_ApplySkin sets.
	const auto field = std::make_shared<TextField>("TextField", "CaretTestField");
	field->SetProperty("Font", "CaretTestFont");
	REQUIRE(field->GetFont() != nullptr);
	field->SetSize(600.0f, 128.0f);
	field->SetTextAreaOffset(Rect(34.0f, 36.0f, 34.0f, 36.0f));
	field->SetText("kyoril");

	const Rect frame = field->GetAbsoluteFrameRect();
	const Rect area = field->GetTextAreaRect();
	const float textScale = FrameManager::Get().GetTextScale();
	const float textWidth = PenAdvance(*field->GetFont(), "kyoril", textScale);
	REQUIRE(textWidth > 0.0f);

	SECTION("the text area insets are converted from UI units to pixels")
	{
		CHECK(area.left - frame.left == Approx(17.0f));
		CHECK(frame.right - area.right == Approx(17.0f));
		CHECK(area.top - frame.top == Approx(18.0f));
		CHECK(field->GetVisibleTextWidth() == Approx(300.0f - 34.0f));
	}

	SECTION("the caret offset is measured from the text area, not from the frame")
	{
		// The renderer draws the text from area.left and the caret at area.left + GetCursorOffset().
		// The offset used to include the inset a second time, which put the caret 17 px (one to two
		// characters) right of the last glyph at 1080p.
		field->OnKeyDown(0x23);    // VK_END
		CHECK(field->GetCursorOffset() == Approx(textWidth));

		field->OnKeyDown(0x24);    // VK_HOME
		CHECK(field->GetCursorOffset() == Approx(0.0f));
	}

	SECTION("clicks map to caret positions with the same scaled inset")
	{
		const float insetLeft = area.left - frame.left;

		// Inside the left inset.
		CHECK(field->GetCursorAt(Point(insetLeft - 1.0f, 30.0f)) == 0);

		// Halfway into the first glyph keeps the caret before it, past the text puts it at the end.
		const float firstAdvance = PenAdvance(*field->GetFont(), "k", textScale);
		CHECK(field->GetCursorAt(Point(insetLeft + firstAdvance * 0.5f, 30.0f)) == 0);
		CHECK(field->GetCursorAt(Point(insetLeft + firstAdvance + 0.5f, 30.0f)) == 1);
		CHECK(field->GetCursorAt(Point(insetLeft + textWidth + 4.0f, 30.0f)) == 6);

		// Just inside the last glyph keeps the caret before it. The old code started counting at the
		// unscaled 34-unit inset instead of 17 px, so every click landed about one glyph too far left.
		CHECK(field->GetCursorAt(Point(insetLeft + textWidth - 0.5f, 30.0f)) == 5);
	}
}
