// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "assets/asset_registry.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/layout_xml_loader.h"
#include "frame_ui/imagery_section.h"
#include "frame_ui/image_component.h"
#include "frame_ui/button.h"
#include "frame_ui/default_renderer.h"
#include "frame_ui/progress_bar.h"
#include "frame_ui/scroll_bar.h"
#include "frame_ui/combo_box.h"
#include "expat/lib/expat.h"
#include <filesystem>
#include "frame_ui/state_imagery.h"
#include "frame_ui/geometry_buffer.h"
#include "graphics/texture_mgr.h"
#include "xml_handler/xml_attributes.h"
#include "lua.hpp"

#include "log/default_log_levels.h"
#include "graphics/render_texture.h"
#include <algorithm>
#include <fstream>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace mmo;

namespace
{
	struct ThemeFixture
	{
		ThemeFixture(bool render = false, uint16 width = 1920, uint16 height = 1080)
		{
			if (!GraphicsDevice::HasInstance())
			{
#ifdef _WIN32
				if (render)
				{
					GraphicsDeviceDesc desc;
					desc.width = width;
					desc.height = height;
					desc.customWindowHandle = CreateWindowExW(0, L"STATIC", L"Theme preview", WS_POPUP,
						0, 0, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
					GraphicsDevice::CreateD3D11(desc);
				}
				else
#endif
				{
					GraphicsDevice::CreateNull(GraphicsDeviceDesc());
				}
			}
			AssetRegistry::Initialize(MMO_TEST_CLIENT_DATA_DIR, {});
			lua = luaL_newstate();
			luaL_openlibs(lua);
			luabind::open(lua);
			FrameManager::Initialize(lua, localization);
			FrameManager::Get().NotifyScreenSizeChanged(static_cast<float>(width), static_cast<float>(height));
		}

		~ThemeFixture()
		{
			FrameManager::Get().ResetTopFrame();
			FrameManager::Destroy();
			lua_close(lua);
			AssetRegistry::Destroy();
		}

		lua_State* lua = nullptr;
		Localization localization;
	};

	class ProgressAreaProbe final : public FrameComponent
	{
	public:
		ProgressAreaProbe(Frame& frame, std::vector<Rect>& areas)
			: FrameComponent(frame)
			, m_areas(areas)
		{
		}

		std::unique_ptr<FrameComponent> Copy() const override
		{
			return std::make_unique<ProgressAreaProbe>(*m_frame, m_areas);
		}

		void Render(const Rect& area, const Color&) override
		{
			m_areas.push_back(area);
		}

	private:
		std::vector<Rect>& m_areas;
	};
}

TEST_CASE("Progress fills stay inside their inset housing at low and full values", "[theme][progress]")
{
	ThemeFixture fixture;
	auto& manager = FrameManager::Get();
	auto frame = manager.Create("ProgressBar", "InsetProgress");
	auto* bar = static_cast<ProgressBar*>(frame.get());
	bar->SetSize(200, 40);
	std::vector<Rect> areas;
	ImagerySection section("Fill");
	section.AddComponent(std::make_unique<ProgressAreaProbe>(*bar, areas));
	FrameLayer layer;
	layer.AddSection(*bar->AddImagerySection(section));
	StateImagery state("Progress");
	state.AddLayer(layer);
	bar->AddStateImagery(state);

	const float inset = GENERATE(0.0f, 10.0f);
	bar->SetProperty("ProgressInset", inset == 0 ? "0" : "10");
	const auto scale = manager.GetUIScale();
	const Rect bounds = bar->GetAbsoluteFrameRect();
	for (const float progress : { 0.01f, 0.5f, 1.0f })
	{
		INFO("Inset: " << inset << ", progress: " << progress);
		areas.clear();
		bar->SetProgress(progress);
		bar->Render();
		REQUIRE(areas.size() == 1);
		const Rect& fill = areas.front();
		CHECK(fill.left == Approx(bounds.left + inset * scale.x));
		CHECK(fill.top == Approx(bounds.top + inset * scale.y));
		CHECK(fill.bottom == Approx(bounds.bottom - inset * scale.y));
		CHECK(fill.GetWidth() == Approx((bounds.GetWidth() - 2 * inset * scale.x) * progress));
		CHECK(fill.right <= bounds.right - inset * scale.x + 0.001f);
		CHECK(fill.GetWidth() > 0);
	}
	areas.clear();
	bar->SetProgress(0);
	bar->Render();
	CHECK(areas.empty());
	bar->SetProperty("ProgressInset", "100");
	bar->SetProgress(1);
	bar->Render();
	CHECK(areas.empty());
}

TEST_CASE("Glue templates define the fonts and controls the screens inherit", "[theme][glue]")
{
	ThemeFixture fixture;
	auto& manager = FrameManager::Get();
	manager.SetTopFrame(manager.Create("Frame", "GlueThemeTestRoot"));
	manager.LoadUIFile("Interface/GlueUI/GlueTemplates.xml");

	REQUIRE(manager.GetFontMap("DefaultHeader") != nullptr);
	REQUIRE(manager.GetFontMap("GlueTitleFont") != nullptr);

	auto button = manager.Find("GlueButton");
	REQUIRE(button != nullptr);
	CHECK(button->GetImagerySectionByName("Rest") != nullptr);
	CHECK(button->GetStateImageryByName("Disabled") != nullptr);

	// Every primary button carries the hover glint as its first child.
	auto primary = manager.Find("GlueButtonPrimary");
	REQUIRE(primary != nullptr);
	REQUIRE(primary->GetChildCount() == 1);
	CHECK(primary->GetChild(0)->HasAnimation("Glint"));

	auto field = manager.Find("GlueTextField");
	REQUIRE(field != nullptr);
	CHECK(field->GetProperty("RimColor") != nullptr);
}

TEST_CASE("Appearance copies replace imagery without changing behavior or layout", "[theme]")
{
	ThemeFixture fixture;
	auto source = FrameManager::Get().Create("Frame", "Appearance");
	auto target = FrameManager::Get().Create("Button", "Consumer");
	target->SetSize(180.0f, 64.0f);
	target->SetText("Kept");
	target->SetProperty("Visible", "false");
	auto child = FrameManager::Get().Create("Frame", "Child");
	target->AddChild(child);
	ImagerySection obsolete("Obsolete");
	target->AddImagerySection(obsolete);
	StateImagery oldState("OldState");
	target->AddStateImagery(oldState);

	// A texture binding must observe the consumer after copying, never the prototype.
	const std::string white = "Textures/Character/T_EV_BlankWhite_01.htex";
	const std::string slot = "Interface/GameUI/Alestia/ActionSocket.htex";
	source->AddProperty("Icon", white);
	target->AddProperty("Icon", slot);
	ImagerySection section("Image");
	auto image = std::make_unique<ImageComponent>(*source, white);
	image->SetImagePropertyName("Icon");
	section.AddComponent(std::move(image));
	auto* sourceSection = source->AddImagerySection(section);
	FrameLayer layer;
	layer.AddSection(*sourceSection);
	StateImagery state("Normal");
	state.AddLayer(layer);
	source->AddStateImagery(state);

	source->CopyVisuals(*target);
	CHECK(target->GetType() == "Button");
	CHECK(target->GetText() == "Kept");
	CHECK_FALSE(target->IsVisible());
	CHECK(target->GetChildCount() == 1);
	CHECK(target->GetWidth() == Approx(180.0f));
	CHECK(target->GetImagerySectionByName("Obsolete") == nullptr);
	CHECK(target->GetStateImageryByName("OldState") == nullptr);
	REQUIRE(target->GetStateImageryByName("Normal") != nullptr);
	target->GetStateImageryByName("Normal")->Render(Rect(0, 0, 90, 32), Color::White);
	CHECK(target->GetGeometryBuffer().GetActiveTexture() == TextureManager::Get().CreateOrRetrieve(slot));
	target->SetProperty("Icon", white);
	target->GetStateImageryByName("Normal")->Render(Rect(0, 0, 90, 32), Color::White);
	CHECK(target->GetGeometryBuffer().GetActiveTexture() == TextureManager::Get().CreateOrRetrieve(white));

	// Destroying prototype sections must not leave consumer state layers dangling.
	source->RemoveStateImagery("Normal");
	source->RemoveImagerySection("Image");
	target->GetStateImageryByName("Normal")->Render(Rect(0, 0, 90, 32), Color::White);
	CHECK(target->GetGeometryBuffer().GetVertexCount() == 18);
	target->CopyVisuals(*target);
	CHECK(target->GetStateImageryByName("Normal") != nullptr);
}

TEST_CASE("Visual inheritance uses the native XML loader and preserves the owning frame type", "[theme]")
{
	ThemeFixture fixture;
	auto prototype = FrameManager::Get().Create("Frame", "Appearance");
	ImagerySection section("Background");
	prototype->AddImagerySection(section);
	LayoutXmlLoader loader;
	loader.SetFilename("appearance-test.xml");
	XmlAttributes frame;
	frame.Add("name", "ButtonWithAppearance");
	frame.Add("type", "Button");
	loader.ElementStart("Frame", frame);
	XmlAttributes appearance;
	appearance.Add("inherits", "Appearance");
	loader.ElementStart("Visual", appearance);
	loader.ElementEnd("Visual");
	loader.ElementEnd("Frame");
	auto button = FrameManager::Get().Find("ButtonWithAppearance");
	REQUIRE(button != nullptr);
	CHECK(button->GetType() == "Button");
	CHECK(button->GetImagerySectionByName("Background") != nullptr);
}

TEST_CASE("BlueGold theme manifest loads native visual templates and font roles", "[theme]")
{
	std::vector<std::string> errors;
	scoped_connection logs { g_DefaultLog.signal().connect([&](const LogEntry& entry)
	{
		if (entry.level == &ErrorLevel)
		{
			errors.push_back(entry.message);
		}
	}) };
	// Legacy is not covered: its Fonts.xml needs FRIZQT__.TTF, which is not in the repository.
	const std::string theme = GENERATE("BlueGold");
	ThemeFixture fixture;
	auto& manager = FrameManager::Get();
	manager.SetTopFrame(manager.Create("Frame", "ThemeTestRoot"));
	manager.LoadUIFile("Interface/GameUI/Themes/" + theme + "/Theme.toc");
	REQUIRE(manager.GetFontMap("CharacterTitleFont") != nullptr);
	REQUIRE(manager.GetFontMap("CharacterBodyFont") != nullptr);
	REQUIRE(manager.Find("ThemeGlueButtonVisual") != nullptr);
	REQUIRE(manager.Find("ThemeMinimapBorderVisual") != nullptr);
	REQUIRE(manager.Find("ThemeChatBubbleTemplateVisual") != nullptr);
	if (theme == "BlueGold")
	{
		// The large circular source is reduced several times on the HUD. Without
		// mipmaps its fine alpha edges alias even with linear texture filtering.
		auto minimap = manager.Find("ThemeMinimapBorderVisual");
		auto imagery = minimap->GetStateImageryByName("Enabled");
		REQUIRE(imagery != nullptr);
		imagery->Render(Rect(0, 0, 256, 256), Color::White);
		auto ring = minimap->GetGeometryBuffer().GetActiveTexture();
		REQUIRE(ring != nullptr);
		CHECK(ring->HasMipMaps());
		CHECK(ring->GetTextureFilter() != TextureFilter::None);
	}

	// Shared behavioral templates must accept either appearance contract.
	manager.LoadUIFile("Interface/GameUI/GameTemplates.xml");
	auto panel = manager.Find("SidePanelTemplate");
	REQUIRE(panel != nullptr);
	CHECK(panel->GetImagerySectionByName("Background") != nullptr);
	CHECK(panel->GetChildCount() == 1);
	auto title = manager.Find("TitleBar");
	REQUIRE(title != nullptr);
	CHECK(title->GetFont() != nullptr);
	CHECK(title->GetChildCount() == 3);
	manager.LoadUIFile("Interface/GameUI/InventoryButton.xml");
	REQUIRE(manager.Find("InventoryItemButtonTemplate") != nullptr);
	for (const auto& error : errors)
	{
		FAIL_CHECK(error);
	}
}



#ifdef _WIN32
// Run separately: set MMO_THEME_PREVIEW_OUTPUT and optionally MMO_THEME_PREVIEW=Legacy.
// Hidden from the normal suite because this deliberately uses a real D3D device.
TEST_CASE("Render a native theme review sheet", "[.theme_preview]")
{
	const char* output = std::getenv("MMO_THEME_PREVIEW_OUTPUT");
	REQUIRE(output != nullptr);
	ThemeFixture fixture(true);
	auto& manager = FrameManager::Get();
	auto root = manager.Create("Frame", "ThemePreviewRoot");
	root->SetSize(3840, 2160);
	manager.SetTopFrame(root);
	const char* requestedTheme = std::getenv("MMO_THEME_PREVIEW");
	const std::string theme = requestedTheme ? requestedTheme : "BlueGold";
	manager.LoadUIFile("Interface/GameUI/Themes/" + theme + "/Theme.toc");
	manager.LoadUIFile("Interface/GameUI/GameTemplates.xml");

	const auto make = [&](const std::string& name, const std::string& type,
		const std::string& visual, const std::string& renderer,
		float x, float y, float width, float height)
	{
		auto frame = manager.Create(type, name);
		root->AddChild(frame);
		frame->SetRenderer(renderer);
		frame->SetPosition(Point(x, y));
		frame->SetSize(width, height);
		if (!visual.empty())
		{
			auto prototype = manager.Find(visual);
			REQUIRE(prototype != nullptr);
			prototype->CopyVisuals(*frame);
		}
		return frame;
	};
	const auto label = [&](const std::string& name, const std::string& text, float x, float y, const std::string& font)
	{
		auto frame = make(name, "Frame", "", "DefaultRenderer", x, y, 1000, 70);
		manager.Find("TextSmall")->Copy(*frame);
		frame->ClearAnchors();
		frame->SetPosition(Point(x, y));
		frame->SetSize(1000, 70);
		frame->SetProperty("TextColor", "FFF3EAD2");
		frame->SetProperty("Font", font);
		frame->SetText(text);
		return frame;
	};
	make("PanelPreview", "Frame", "ThemeInventoryFrameVisual", "DefaultRenderer", 140, 180, 1450, 1580);
	make("InventoryPreview", "Frame", "ThemeInventoryFrameVisual", "DefaultRenderer", 1730, 180, 1950, 850);
	label("Heading", theme + " / Native game UI", 160, 60, "DefaultHeader");
	label("PanelTitle", "Character", 180, 210, "CharacterTitleFont");
	label("InventoryTitle", "Inventory", 1770, 210, "CharacterTitleFont");
	const std::string icons[] = {"S_Fireball", "S_FireNova", "T_Icon_Fire_01", "T_Icon_Fire_02"};
	for (int i = 0; i < 18; ++i)
	{
		auto slot = make("Slot" + std::to_string(i), "Button", "ThemeCraftingReagentIconTemplateVisual", "ButtonRenderer",
			1780.0f + (i % 9) * 205, 340.0f + (i / 9) * 205, 190, 190);
		slot->AddProperty("IconColor", "FFFFFFFF");
		slot->AddProperty("BackgroundColor", "FFFFFFFF");
		slot->AddProperty("QualityColor", "FFFFFFFF");
		slot->AddProperty("Background");
		slot->AddProperty("Icon", i < 8 ? "Interface/Icons/Spells/" + icons[i % 4] + ".htex" : "");
		// Install the visual after property declarations so dynamic bindings can attach.
		manager.Find("ThemeCraftingReagentIconTemplateVisual")->CopyVisuals(*slot);
	}
	for (int i = 0; i < 4; ++i)
	{
		auto button = make("Button" + std::to_string(i), "Button", "", "ButtonRenderer", 230, 420.0f + i * 170, 1260, 130);
		manager.Find("GlueButton")->Copy(*button);
		button->ClearAnchors();
		button->SetPosition(Point(230, 420.0f + i * 170));
		button->SetSize(1260, 130);
		button->SetText(i == 0 ? "Normal" : i == 1 ? "Hovered" : i == 2 ? "Pressed" : "Disabled");
		auto* typedButton = static_cast<Button*>(button.get());
		typedButton->SetButtonState(i == 1 ? ButtonState::Hovered : i == 2 ? ButtonState::Pushed : ButtonState::Normal);
		button->SetProperty("Enabled", i == 3 ? "false" : "true");
	}
	label("BodyPreview", "Stamina             38", 240, 1210, "CharacterBodyFont");
	label("BodyPreview2", "Strength            21", 240, 1290, "CharacterBodyFont");
	label("BodyPreview3", "Armor              181", 240, 1370, "CharacterBodyFont");
	label("Notes", "Beveled brass / textured navy / recessed slots", 1760, 1150, "DefaultSmall");
	make("BarPreview", "Frame", "ThemeInventoryFrameVisual", "DefaultRenderer", 1730, 1330, 1950, 240);

	auto& device = GraphicsDevice::Get();
	auto texture = device.CreateRenderTexture("ThemeReview", 1920, 1080, RenderTextureFlags::HasColorBuffer);
	device.Reset();
	texture->Activate();
	device.SetClearColor(0xFF151D2B);
	texture->Clear(ClearFlags::Color);
	device.SetViewport(0, 0, 1920, 1080, 0, 1);
	manager.Draw();
	std::vector<uint8> pixels(1920 * 1080 * 4);
	texture->CopyPixelDataTo(pixels.data());
	REQUIRE(std::any_of(pixels.begin(), pixels.end(), [](uint8 value) { return value != 0; }));
	std::ofstream file(output, std::ios::binary);
	file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
	REQUIRE(file.good());
}
#endif

#ifdef _WIN32
namespace
{
	/// Exercise production geometry and styles, without requiring a connected world server.
	/// Script-created rows are populated below using their shipped control templates.
	struct WindowPreviewLoader
	{
		LayoutXmlLoader loader;
		int skippedDepth = 0;

		static void Start(void* context, const char* name, const char** attributes)
		{
			auto& self = *static_cast<WindowPreviewLoader*>(context);
			const std::string tag(name);
			if (self.skippedDepth > 0 || tag == "Script" || tag == "Scripts" || tag == "Events")
			{
				++self.skippedDepth;
				return;
			}
			if (tag == "UiLayout")
			{
				return;
			}
			XmlAttributes map;
			for (const char** attribute = attributes; attribute && *attribute; attribute += 2)
			{
				map.Add(attribute[0], attribute[1]);
			}
			self.loader.ElementStart(tag, map);
		}

		static void End(void* context, const char* name)
		{
			auto& self = *static_cast<WindowPreviewLoader*>(context);
			if (self.skippedDepth > 0)
			{
				--self.skippedDepth;
				return;
			}
			if (std::string(name) != "UiLayout")
			{
				self.loader.ElementEnd(name);
			}
		}

		static void Text(void* context, const char* text, int length)
		{
			auto& self = *static_cast<WindowPreviewLoader*>(context);
			if (self.skippedDepth == 0)
			{
				self.loader.Text(std::string(text, length));
			}
		}

		void Load(const std::string& name)
		{
			const std::string path = "Interface/GameUI/" + name + ".xml";
			auto file = AssetRegistry::OpenFile(path);
			REQUIRE(file != nullptr);
			const std::string content((std::istreambuf_iterator<char>(*file)), std::istreambuf_iterator<char>());
			loader.SetFilename(path);
			XML_Parser parser = XML_ParserCreate(nullptr);
			XML_SetUserData(parser, this);
			XML_SetElementHandler(parser, Start, End);
			XML_SetCharacterDataHandler(parser, Text);
			const auto result = XML_Parse(parser, content.data(), static_cast<int>(content.size()), true);
			XML_ParserFree(parser);
			REQUIRE(result == XML_STATUS_OK);
		}
	};
}

TEST_CASE("Render production character inventory options and HUD layouts", "[.theme_windows]")
{
	const char* output = std::getenv("MMO_THEME_PREVIEW_OUTPUT");
	REQUIRE(output != nullptr);
	ThemeFixture fixture(true, 2560, 1440);
	AssetRegistry::AddArchivePackage(std::filesystem::path(MMO_TEST_CLIENT_DATA_DIR) / "Locales/Locale_enUS");
	REQUIRE(fixture.localization.LoadFromFile());
	auto& manager = FrameManager::Get();
	const char* themeValue = std::getenv("MMO_THEME_PREVIEW");
	const std::string theme = themeValue ? themeValue : "BlueGold";
	const char* sceneValue = std::getenv("MMO_THEME_PREVIEW_SCENE");
	const std::string scene = sceneValue ? sceneValue : "character";
	auto root = manager.Create("Frame", "TopGameFrame");
	root->SetSize(3840, 2160);
	manager.SetTopFrame(root);
	for (const std::string type : { "World", "UnitModel", "Cooldown", "Minimap" })
	{
		manager.RegisterFrameFactory(type, [type](const std::string& name) { return std::make_shared<Frame>(type, name); });
	}
	for (const std::string renderer : { "WorldRenderer", "ModelRenderer" })
	{
		manager.RegisterFrameRenderer(renderer, [](const std::string& name) { return std::make_unique<DefaultRenderer>(name); });
	}
	manager.LoadUIFile("Interface/GameUI/Themes/" + theme + "/Theme.toc");
	WindowPreviewLoader loader;
	for (const std::string name : { "GameTemplates", "GameParent", "GameMenuBar", "ActionBarButton", "ActionBar", "InventoryButton", "CharacterWindow", "MoneyFrame", "InventoryFrame", "PlayerFrame", "TargetFrame", "OptionsFrame", "Minimap" })
	{
		loader.Load(name);
	}
	const auto find = [&](const std::string& name)
	{
		auto frame = manager.Find(name);
		REQUIRE(frame != nullptr);
		return frame;
	};
	const auto clone = [&](const std::string& prototype, const FramePtr& parent, float x, float y, float width, float height)
	{
		auto frame = find(prototype)->Clone();
		parent->AddChild(frame);
		frame->ClearAnchors();
		frame->SetPosition(Point(x, y));
		frame->SetSize(width, height);
		frame->Show();
		return frame;
	};
	find("WorldFrame")->Hide();
	find("CharacterWindow")->SetVisible(scene == "character");
	find("CharacterClassesPage")->Hide();
	find("CharacterAttributesPage")->Show();
	find("OptionsFrame")->SetVisible(scene == "options");
	find("InventoryFrame")->SetVisible(scene == "character");
	find("CharacterWindowTitle")->SetText("Arwyn");
	find("CharacterLevelLabel")->SetText("Level 5");
	find("CharacterWindowAttributePointsLabel")->SetText("1");
	static_cast<Button*>(find("CharacterWindowTab1").get())->SetChecked(true);
	const std::string attributes[] = { "Stamina", "Strength", "Agility", "Intellect", "Spirit" };
	for (int i = 0; i < 5; ++i)
	{
		auto row = clone("CharacterStatLabelTemplate", find("CharacterWindowPrimaryStatsFrame"), 16, 48.0f + i * 52, 220, 44);
		row->SetText(attributes[i]);
		auto value = clone("CharacterStatValueTemplate", find("CharacterWindowPrimaryStatsFrame"), 290, 48.0f + i * 52, 76, 44);
		value->SetText(std::to_string(21 + i));
		auto add = clone("CharacterAttributeButtonTemplate", find("CharacterWindowPrimaryStatsFrame"), 382, 48.0f + i * 52, 44, 44);
		add->SetText("+");
	}
	for (const std::string name : { "CharacterArmorStat", "CharacterDamageStat", "CharacterAttackPowerStat", "CharacterAttackTimeStat", "CharacterMeleeCritStat" })
	{
		find(name)->SetText(name == "CharacterDamageStat" ? "16 - 19" : name == "CharacterAttackTimeStat" ? "1.60" : "104");
	}
	const std::string icons[] = { "S_Fireball", "S_FireNova", "T_Icon_Fire_01", "T_Icon_Fire_02", "T_Icon_Fire_03", "T_Icon_Fire_04" };
	for (int i = 0; i < 12; ++i)
	{
		auto frame = find("ActionButton" + std::to_string(i + 1));
		frame->SetProperty("Icon", i < 8 ? "Interface/Icons/Spells/" + icons[i % 6] + ".htex" : "");
		frame->GetChild(1)->SetText(std::to_string((i + 1) % 10));
	}
	for (int i = 0; i < 9; ++i)
	{
		auto frame = clone("GameMenuBarImageButton", find("MenuBarButtons"), static_cast<float>(i * 70), 0, 70, 104);
		const std::string menuIcons[] = { "Character", "Repertoire", "Professions", "Emotes", "Inventory", "Quests", "Talents", "Menu", "Menu" };
		// Use the actual production menu artwork, not placeholder glyphs.
		frame->SetProperty("Icon", "Interface/GameUI/Alestia/MenuIcons/" + menuIcons[i] + ".htex");
	}
	find("InventoryFrame")->SetSize(973, 577);
	find("InventoryTitleBar")->SetText("Inventory");
	for (int bag = 0; bag < 4; ++bag)
	{
		auto frame = clone("InventoryItemButtonTemplate", find("BagContainer"), 0, static_cast<float>(bag * 102), 102, 102);
		frame->SetProperty("Background", "Interface/Icons/ItemSlots/icons_01_bag.htex");
	}
	const std::string items[] = { "acolyte_robe_icon", "acolyte_staff_icon", "arcane_cord_icon", "ashen_quarterstaff_icon", "barrowfront_mail_hauberk_icon", "belt_of_sanctity_icon", "berserker_ring_icon", "boots_of_the_templar_icon", "cadet_shield_icon", "cloak_of_radiant_devotion_icon" };
	const std::string equipment[] = { "CharacterInvSlotChest", "CharacterInvSlotMainHand", "CharacterInvSlotWaist", "CharacterInvSlotFeet", "CharacterInvSlotBack" };
	for (int i = 0; i < 5; ++i)
	{
		find(equipment[i])->SetProperty("Icon", "Interface/Icons/Items/" + items[i] + ".htex");
		find(equipment[i])->SetProperty("BackgroundColor", "00000000");
		find(equipment[i])->SetProperty("QualityColor", i == 0 ? "FF6FAFC4" : "FFB59D73");
	}
	for (int i = 0; i < 32; ++i)
	{
		auto frame = clone("InventoryItemButtonTemplate", find("ItemSlotContainer"), static_cast<float>((i % 8) * 102), static_cast<float>((i / 8) * 102), 102, 102);
		if (i < 12)
		{
			frame->SetProperty("Icon", "Interface/Icons/Items/" + items[i % 10] + ".htex");
			frame->SetProperty("QualityColor", i % 3 == 0 ? "FF6FAFC4" : "FFFFFFFF");
		}
	}
	for (const std::string name : { "PlayerHealthBar", "TargetHealthBar", "PlayerManaBar", "TargetManaBar", "PlayerExperienceBar", "PlayerClassExperienceBar" })
	{
		auto frame = find(name);
		frame->Show();
		static_cast<ProgressBar*>(frame.get())->SetProgress(name.find("Experience") != std::string::npos ? 0.45f : 0.85f);
		frame->SetText(name.find("Experience") != std::string::npos ? (name.find("Class") != std::string::npos ? "Class Experience: 271 / 400" : "Character Experience: 48 / 2800") : "99 / 99");
	}
	// Run the real shared XP layout function so theme metrics and second-bar spacing are exercised.
	const auto scriptPath = std::filesystem::path(MMO_TEST_CLIENT_DATA_DIR) / "Interface/GameUI/GameMenuBar.lua";
	const int scriptResult = luaL_dofile(fixture.lua, scriptPath.string().c_str());
	REQUIRE(scriptResult == 0);
	lua_getglobal(fixture.lua, "GameMenuBar_UpdateExperienceBarLayout");
	REQUIRE(lua_pcall(fixture.lua, 0, 0, 0) == 0);
	find("PlayerName")->SetText("Arwyn");
	find("TargetName")->SetText("Training Dummy");
	find("MinimapTitleBar")->SetText("Oakenshire");
	find("MinimapTime")->SetText("14:19");
	find("OptionsTitleBar")->SetText("Options");
	const std::string categories[] = { "Graphics", "Sound", "Interface", "Gameplay", "Key Bindings" };
	for (int i = 0; i < 5; ++i)
	{
		auto frame = clone("OptionsCatButtonTemplate", find("OptionsCategoryPanel"), 8, 8.0f + i * 96, 364, 90);
		frame->SetText(categories[i]);
		static_cast<Button*>(frame.get())->SetChecked(i == 0);
	}
	const std::string labels[] = { "Monitor", "Display Mode", "Resolution", "Render Scale", "VSync", "Max Foreground FPS", "Max Background FPS", "Brightness" };
	for (int i = 0; i < 8; ++i)
	{
		const bool toggle = i == 4;
		const bool slider = i >= 5;
		auto row = clone(toggle ? "OptionsToggleRowTemplate" : slider ? "OptionsSliderRowTemplate" : "OptionsComboRowTemplate", find("OptionsScrollContent"), 0, 100.0f + i * 90, 1630, 80);
		row->GetChild(0)->SetText(labels[i]);
		if (!toggle && !slider)
		{
			auto* combo = static_cast<ComboBox*>(row->GetChild(1));
			combo->AddItem(i == 0 ? "Primary Monitor" : i == 1 ? "Fullscreen (Window)" : i == 2 ? "2560 x 1440" : "100%", "");
			combo->SetSelectedIndex(0);
		}
		if (toggle)
		{
			static_cast<Button*>(row->GetChild(1))->SetChecked(true);
		}
		if (slider)
		{
			auto* bar = static_cast<ScrollBar*>(row->GetChild(1));
			bar->SetMinimumValue(0); bar->SetMaximumValue(100); bar->SetValue(60);
			row->GetChild(2)->SetText(i == 7 ? "1.00" : "60 FPS");
		}
	}
	auto heading = clone("OptionsHeaderRowTemplate", find("OptionsScrollContent"), 0, 0, 1630, 96);
	heading->GetChild(0)->SetText("Display");
	find("PlayerManaBar")->SetProperty("ProgressColor", "FF3686C7");
	find("TargetManaBar")->SetProperty("ProgressColor", "FF3686C7");
	find("OptionsDefaultsButton")->SetText("Defaults");
	find("OptionsOkayButton")->SetText("Okay");
	find("OptionsCancelButton")->SetText("Cancel");

	auto& device = GraphicsDevice::Get();
	auto texture = device.CreateRenderTexture("GameWindowsReview", 2560, 1440, RenderTextureFlags::HasColorBuffer);
	device.Reset(); texture->Activate(); texture->Clear(ClearFlags::Color); device.SetViewport(0, 0, 2560, 1440, 0, 1);
	manager.Draw();
	std::vector<uint8> pixels(2560 * 1440 * 4);
	texture->CopyPixelDataTo(pixels.data());
	REQUIRE(std::any_of(pixels.begin(), pixels.end(), [](uint8 value) { return value != 0; }));
	std::ofstream file(output, std::ios::binary);
	file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
	REQUIRE(file.good());
}
#endif
