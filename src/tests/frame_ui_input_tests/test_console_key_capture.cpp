// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "frame_ui/button.h"
#include "frame_ui/frame.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/localization.h"
#include "frame_ui/textfield.h"
#include "graphics/graphics_device.h"

#include "lua.hpp"

using namespace mmo;

namespace
{
	/// Owns a Lua state and a FrameManager for the duration of a test, as the keyboard capture
	/// lives in the frame manager.
	struct CaptureFixture
	{
		CaptureFixture()
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

		~CaptureFixture()
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

TEST_CASE("Only a text input frame claims typed characters while it captures the keyboard", "[frame_input][console_key]")
{
	CaptureFixture fixture;
	auto& frameMgr = FrameManager::Get();

	// Buttons are focusable, so clicking one makes it the keyboard capture frame and it stays
	// that way until something else captures the input. It does not turn typed characters into
	// text though, so a typed key still belongs to the game (the console key opens the console).
	const auto button = std::make_shared<Button>(Button::Type, "ConsoleKeyTestButton");
	button->CaptureInput();
	REQUIRE(frameMgr.GetCaptureFrame() == button);
	CHECK_FALSE(frameMgr.GetCaptureFrame()->AcceptsTextInput());

	// The same holds for a plain frame which was made focusable to receive enter or escape.
	const auto focusableFrame = std::make_shared<Frame>("Frame", "ConsoleKeyTestViewport");
	focusableFrame->GetProperty("Focusable")->Set("true");
	focusableFrame->CaptureInput();
	REQUIRE(frameMgr.GetCaptureFrame() == focusableFrame);
	CHECK_FALSE(frameMgr.GetCaptureFrame()->AcceptsTextInput());

	// A text field does consume typed characters, so the key has to go to the text field.
	const auto textField = std::make_shared<TextField>("TextField", "ConsoleKeyTestField");
	textField->CaptureInput();
	REQUIRE(frameMgr.GetCaptureFrame() == textField);
	CHECK(frameMgr.GetCaptureFrame()->AcceptsTextInput());
}
