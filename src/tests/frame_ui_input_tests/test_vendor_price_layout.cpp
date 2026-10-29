// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "assets/asset_registry.h"
#include "frame_ui/font_mgr.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/localization.h"
#include "graphics/graphics_device.h"

#include "lua.hpp"

#include <filesystem>
#include <string>

using namespace mmo;

namespace
{
	/// Owns a Lua state, a FrameManager running at 1360x768 (the resolution the overlap was
	/// reported from) and the client data directory as asset root, so the shipped vendor layout
	/// can be loaded with its real fonts. The locale directory is a search path of its own in the
	/// client, which is how "Fonts/FRIZQT__.TTF" from GameTemplates.xml resolves.
	struct VendorLayoutFixture
	{
		VendorLayoutFixture()
		{
			if (!GraphicsDevice::HasInstance())
			{
				GraphicsDevice::CreateNull(GraphicsDeviceDesc());
			}

			AssetRegistry::Initialize(MMO_TEST_CLIENT_DATA_DIR, { "Locales/Locale_enUS" });

			lua = luaL_newstate();
			luaL_openlibs(lua);
			luabind::open(lua);
			FrameManager::Initialize(lua, localization);
			FrameManager::Get().NotifyScreenSizeChanged(1360.0f, 768.0f);
		}

		~VendorLayoutFixture()
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

	/// Stands in for the client side of the vendor API. The item names are the ones a merchant of
	/// the starting zone offers, because the name length is what drives the word wrapping under
	/// test.
	const char* g_vendorStubs = R"lua(
		function Localize(key) return key; end
		ItemQualityColors = { "FFFFFFFF", "FFFFFFFF", "FF1EFF00", "FF0070DD", "FFA335EE" };

		local items = {
			{ name = "Small Brown Pouch", quality = 1, price = 1250 },
			{ name = "Tome of Rebirth", quality = 2, price = 12345 },
			{ name = "Scroll of Sanctuary Recall", quality = 1, price = 45 },
			{ name = "Rugged Leather Bag", quality = 2, price = 5000 },
			{ name = "Potion of Rejuvenation", quality = 1, price = 250 },
			{ name = "Refreshing Spring Water", quality = 1, price = 25 },
			{ name = "Orange Juice", quality = 1, price = 25 },
			{ name = "Coarse Thread", quality = 1, price = 100 },
		};

		function GetVendorNumItems() return #items; end
		function GetVendorNumBuybackItems() return 0; end

		function GetVendorItemInfo(index)
			local item = items[index + 1];
			if (not item) then return nil, nil, 0, 0; end
			return item, "Interface/Icons/Gold.htex", item.price, 1;
		end

		function GetVendorBuybackItemInfo(index) return nil, nil, 0, 0; end
	)lua";
}

TEST_CASE("The vendor item name never reaches the price row of its card", "[frame_input][vendor_price]")
{
	// The layout comes from the data/client submodule, which a checkout may not have: skip then.
	// (Catch 2 has no SKIP; WARN shows up in the output and the case passes without assertions.)
	const std::filesystem::path layoutPath =
		std::filesystem::path(MMO_TEST_CLIENT_DATA_DIR) / "Interface" / "GameUI" / "VendorFrame.xml";
	if (!std::filesystem::is_regular_file(layoutPath))
	{
		WARN("Skipped: client data layout not found at " << layoutPath.string());
		return;
	}

	VendorLayoutFixture fixture;
	auto& mgr = FrameManager::Get();

	const auto top = mgr.CreateOrRetrieve("Frame", "TopGameFrame");
	top->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr);
	top->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr);
	top->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr);
	top->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr);
	mgr.SetTopFrame(top);

	mgr.ExecuteLua(g_vendorStubs);

	// The vendor window in TOC order: templates and fonts, the game parent it is attached to,
	// the money frame template it builds its price rows from, and the window itself.
	mgr.LoadUIFile("Interface/GameUI/GameTemplates.xml");
	mgr.LoadUIFile("Interface/GameUI/GameParent.xml");
	mgr.LoadUIFile("Interface/GameUI/MoneyFrame.xml");
	mgr.LoadUIFile("Interface/GameUI/VendorFrame.xml");

	REQUIRE(mgr.Find("VendorFrame") != nullptr);
	REQUIRE(mgr.Find("VendorMoney1") != nullptr);

	mgr.ExecuteLua("VendorFrame.mode = VENDOR_MODE_STOCK; VendorFrame.page = 1; VendorFrame_UpdateVendorItems();");

	const float textScale = mgr.GetTextScale();

	for (int i = 1; i <= 8; ++i)
	{
		const auto nameFrame = mgr.Find("VendorText" + std::to_string(i));
		const auto moneyFrame = mgr.Find("VendorMoney" + std::to_string(i));
		REQUIRE(nameFrame != nullptr);
		REQUIRE(moneyFrame != nullptr);

		const FontPtr font = nameFrame->GetFont();
		REQUIRE(font != nullptr);

		const Rect nameRect = nameFrame->GetAbsoluteFrameRect();
		const Rect moneyRect = moneyFrame->GetAbsoluteFrameRect();

		// What TextComponent::RenderTraditional draws: one line per wrapped line, each as tall as
		// the font, starting at the top of the name frame.
		const uint32 lines = font->GetLineCount(nameFrame->GetText(), nameRect, textScale);
		REQUIRE(lines >= 1);

		const float nameTextBottom = nameRect.top + font->GetHeight(textScale) * lines;

		INFO("card " << i << " item '" << nameFrame->GetText() << "' wraps into " << lines << " line(s)");
		CHECK(nameTextBottom <= moneyRect.top);
	}
}
