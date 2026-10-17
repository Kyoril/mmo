// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/localization.h"
#include "frame_ui/textfield.h"
#include "graphics/graphics_device.h"

#include "lua.hpp"

using namespace mmo;

namespace
{
	/// Owns a Lua state and a FrameManager for the duration of a test, as text fields need the
	/// frame manager to exist.
	struct TextFieldFixture
	{
		TextFieldFixture()
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

		~TextFieldFixture()
		{
			FrameManager::Get().SetCaptureWindow(nullptr);
			FrameManager::Get().ResetTopFrame();
			FrameManager::Destroy();
			lua_close(lua);
		}

		lua_State* lua = nullptr;
		Localization localization;
	};
}

TEST_CASE("Control characters from Ctrl shortcuts never become text field content", "[frame_input][textfield_paste]")
{
	TextFieldFixture fixture;

	const auto textField = std::make_shared<TextField>("TextField", "PasteTestField");
	textField->SetTextMasked(true);
	textField->SetText("secret");

	// Windows turns Ctrl+V into a WM_CHAR carrying 0x16 (SYN) right after the key down which
	// performed the paste. The same goes for the other Ctrl shortcuts the field handles. None
	// of them are text, so none of them may be appended to the password.
	textField->OnKeyChar(0x16);     // Ctrl+V
	CHECK(textField->GetText() == "secret");

	textField->OnKeyChar(0x03);     // Ctrl+C
	CHECK(textField->GetText() == "secret");

	textField->OnKeyChar(0x18);     // Ctrl+X
	CHECK(textField->GetText() == "secret");

	textField->OnKeyChar(0x1B);     // Escape
	CHECK(textField->GetText() == "secret");

	textField->OnKeyChar(0x7F);     // Delete control character
	CHECK(textField->GetText() == "secret");

	// Printable characters still have to arrive, otherwise typing would be broken.
	textField->OnKeyChar('!');
	CHECK(textField->GetText() == "secret!");
}

TEST_CASE("A single line text field rejects tabs and line breaks but keeps them when configured", "[frame_input][textfield_paste]")
{
	TextFieldFixture fixture;

	const auto singleLine = std::make_shared<TextField>("TextField", "SingleLinePasteField");
	singleLine->SetText("abc");
	singleLine->OnKeyChar('\t');
	singleLine->OnKeyChar('\r');
	singleLine->OnKeyChar('\n');
	CHECK(singleLine->GetText() == "abc");

	const auto tabField = std::make_shared<TextField>("TextField", "TabPasteField");
	tabField->SetAcceptsTab(true);
	tabField->SetText("abc");
	tabField->OnKeyChar('\t');
	CHECK(tabField->GetText() == "abc\t");
}
