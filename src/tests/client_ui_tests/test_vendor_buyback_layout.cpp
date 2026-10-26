// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "lua/lua.hpp"
#include <string>

TEST_CASE("Responsive vendor pagination uses stock and buyback data independently", "[vendor][buyback]")
{
	auto* lua = luaL_newstate();
	luaL_openlibs(lua);
	const auto run = [lua](const char* code)
	{
		const int result = luaL_dostring(lua, code);
		INFO((result ? lua_tostring(lua, -1) : ""));
		REQUIRE(result == 0);
	};
	run(R"lua(
		function NewFrame()
			return {
				height=400, visible=true,
				SetText=function(self,text) self.text=text; end,
				GetTextHeight=function() return 32; end,
				SetHeight=function(self,height) self.height=height; end,
				GetHeight=function(self) return self.height; end,
				GetLayoutUIScale=function() return {x=1,y=1}; end,
				SetAnchor=function() end,
				SetProperty=function() end,
				Enable=function(self) self.enabled=true; end,
				Disable=function(self) self.enabled=false; end,
				Show=function(self) self.visible=true; end,
				Hide=function(self) self.visible=false; end,
				SetChecked=function(self,value) self.checked=value; end
			};
		end
		AnchorPoint={TOP=1,BOTTOM=2}; ItemQualityColors={};
		function GetUIScale() return {x=1,y=1}; end
		function Localize(key)
			if key=='VENDOR_PAGE_FORMAT' then return '%d/%d'; end
			if key=='VENDOR_STOCK_COUNT' or key=='VENDOR_BUYBACK_COUNT' then return '%d'; end
			return key;
		end
		for _,name in ipairs({'VendorFrame','VendorStockLabel','VendorPurchaseHint','VendorPageLabel',
			'VendorFooter','VendorStockSurface','VendorNextPageButton','VendorPrevPageButton',
			'VendorBuybackTabButton','VendorMoney1_CopperButton'}) do _G[name]=NewFrame(); end
		for i=1,14 do
			_G['VendorButton'..i..'Border']=NewFrame(); _G['VendorText'..i]=NewFrame();
			_G['VendorButton'..i]=NewFrame(); _G['VendorMoney'..i]=NewFrame();
		end
		function GetVendorNumItems() return 9; end
		function GetVendorNumBuybackItems() return 2; end
		local function Item(name) return {name=name,quality=1},'icon',10,1; end
		function GetVendorItemInfo(index)
			assert(VendorFrame.mode==VENDOR_MODE_STOCK);
			return Item('Stock '..index);
		end
		missingBuyback=false;
		function GetVendorBuybackItemInfo(index)
			assert(VendorFrame.mode==VENDOR_MODE_BUYBACK);
			if missingBuyback and index==0 then return nil; end
			return Item('Buyback '..index);
		end
		function RefreshMoneyFrame() end
	)lua");
	const std::string path = std::string(MMO_UI_SCRIPT_DIR) + "/VendorFrame.lua";
	REQUIRE(luaL_loadfile(lua, path.c_str()) == 0);
	const int result = lua_pcall(lua, 0, 0, 0);
	INFO((result ? lua_tostring(lua, -1) : ""));
	REQUIRE(result == 0);
	run(R"lua(
		VendorFrame_SetMode(VENDOR_MODE_STOCK);
		assert(VENDOR_ITEMS_PER_PAGE==4);
		assert(VendorText1.text=='Stock 0' and VendorPageLabel.text=='1/3');
		assert(not VendorButton5Border.visible);
		VendorFrame_NextPage(); assert(VendorText1.text=='Stock 4');
		VendorFrame_ToggleBuyback();
		assert(VendorFrame.page==1 and VendorText1.text=='Buyback 0');
		assert(VendorPageLabel.text=='1/1' and VendorBuybackTabButton.checked);
		assert(not VendorButton3Border.visible);
		missingBuyback=true; VendorFrame_UpdateVendorItems();
		assert(not VendorButton1Border.visible and VendorButton2Border.visible);
		missingBuyback=false; VendorFrame_UpdateVendorItems();
		assert(VendorButton1Border.visible);
		VendorFrame_ToggleBuyback(); assert(VendorText1.text=='Stock 0');
	)lua");
	lua_close(lua);
}
