// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "assets/asset_registry.h"
#include "frame_ui/frame_mgr.h"
#include "lua.hpp"

using namespace mmo;

namespace
{
	struct GlueDialogFixture
	{
		GlueDialogFixture()
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
			auto& manager = FrameManager::Get();
			auto root = manager.Create("Frame", "GlueParent");
			manager.SetTopFrame(root);
			// Art-free templates let the real XML and Lua run without a graphics device or font.
			manager.Create("Frame", "GlueBorderFrame");
			manager.Create("Frame", "GlueTextLarge");
			manager.Create("Button", "GlueButton")->SetSize(400, 100);
			for (const auto* name : { "AccountNameField", "AccountPasswordField" })
			{
				auto field = manager.Create("TextField", name);
				root->AddChild(field);
				field->SetText("test");
				field->SetOnEnterPressed(manager.CompileFunction(name,
					"return function(this) AccountLogin_Login(); end"));
			}
			root->AddChild(manager.Create("Button", "LoginButton"));
			manager.LoadUIFile("Interface/GlueUI/GlueDialog.xml");
			manager.LoadUIFile("Interface/GlueUI/AccountLogin.lua");
			Run("loginAttempts=0; cancelled=0; function RunConsoleCommand(cmd) loginAttempts=loginAttempts+1; end; "
				"loginConnector={CancelConnect=function() cancelled=cancelled+1; end};");
		}

		~GlueDialogFixture()
		{
			FrameManager::Get().ResetTopFrame();
			FrameManager::Destroy();
			lua_close(lua);
			AssetRegistry::Destroy();
		}

		void Run(const char* code)
		{
			const int result = luaL_dostring(lua, code);
			INFO((result ? lua_tostring(lua, -1) : ""));
			REQUIRE(result == 0);
		}

		Localization localization;
		lua_State* lua = nullptr;
	};
}

TEST_CASE("Login errors consume Enter and restore the previous login field", "[frame_input][glue_dialog]")
{
	GlueDialogFixture fixture;
	auto& manager = FrameManager::Get();
	const auto* fieldName = GENERATE("AccountNameField", "AccountPasswordField");
	auto field = manager.Find(fieldName);
	field->CaptureInput();
	manager.NotifyKeyUp(13);
	fixture.Run("assert(loginAttempts==1);");

	const auto* error = GENERATE("AUTH_ERROR", "REALM_DISCONNECTED_ERROR");
	if (std::string(error) == "AUTH_ERROR")
	{
		fixture.Run("AccountLogin_AuthError(nil, 2);");
	}
	else
	{
		fixture.Run("GlueDialog_Show('REALM_DISCONNECTED_ERROR');");
	}
	CHECK(manager.GetCaptureFrame() == manager.Find("GlueDialog"));
	manager.NotifyKeyChar('z');
	CHECK(field->GetText() == "test");
	manager.NotifyKeyUp(13);
	fixture.Run("assert(loginAttempts==1); assert(not GlueDialog:IsVisible()); assert(GlueDialog.which==nil);");
	CHECK(manager.GetCaptureFrame() == field);
	// A separate Enter after dismissal can submit a fresh, intentional login.
	manager.NotifyKeyUp(13);
	fixture.Run("assert(loginAttempts==2);");
}

TEST_CASE("Enter on the connecting dialog cancels without submitting another login", "[frame_input][glue_dialog]")
{
	GlueDialogFixture fixture;
	auto& manager = FrameManager::Get();
	manager.Find("AccountPasswordField")->CaptureInput();
	manager.NotifyKeyUp(13);
	manager.NotifyKeyUp(13);
	fixture.Run("assert(loginAttempts==1); assert(cancelled==1); assert(not GlueDialog:IsVisible());");
}
