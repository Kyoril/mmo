// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#define IMGUI_DEFINE_MATH_OPERATORS
#include "item_editor_window.h"
#include "item_display_editor_window.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "editor_imgui_helpers.h"
#include "game/aura.h"
#include "game/item.h"
#include "game/spell.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace ImGui
{
	static ImVector<ImRect> s_GroupPanelLabelStack;

	void BeginGroupPanel(const char *name, const ImVec2 &size = ImVec2(-1.0f, -1.0f))
	{
		ImGui::BeginGroup();

		const auto itemSpacing = ImGui::GetStyle().ItemSpacing;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

		const auto frameHeight = ImGui::GetFrameHeight();
		ImGui::BeginGroup();

		ImVec2 effectiveSize = size;
		if (size.x < 0.0f)
		{
			effectiveSize.x = ImGui::GetContentRegionAvail().x;
		}
		else
		{
			effectiveSize.x = size.x;
		}
		ImGui::Dummy(ImVec2(effectiveSize.x, 0.0f));

		ImGui::Dummy(ImVec2(frameHeight * 0.5f, 0.0f));
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::BeginGroup();
		ImGui::Dummy(ImVec2(frameHeight * 0.5f, 0.0f));
		ImGui::SameLine(0.0f, 0.0f);
		if (name && *name)
		{
			ImGui::Text(name);
		}
		
		auto labelMin = ImGui::GetItemRectMin();
		auto labelMax = ImGui::GetItemRectMax();
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::Dummy(ImVec2(0.0, frameHeight + itemSpacing.y));
		ImGui::BeginGroup();

		ImGui::PopStyleVar(2);

#if IMGUI_VERSION_NUM >= 17301
		ImGui::GetCurrentWindow()->ContentRegionRect.Max.x -= frameHeight * 0.5f;
		ImGui::GetCurrentWindow()->WorkRect.Max.x -= frameHeight * 0.5f;
		ImGui::GetCurrentWindow()->InnerRect.Max.x -= frameHeight * 0.5f;
#else
		ImGui::GetCurrentWindow()->ContentsRegionRect.Max.x -= frameHeight * 0.5f;
#endif
		ImGui::GetCurrentWindow()->Size.x -= frameHeight;

		auto itemWidth = ImGui::CalcItemWidth();
		ImGui::PushItemWidth(ImMax(0.0f, itemWidth - frameHeight));

		s_GroupPanelLabelStack.push_back(ImRect(labelMin, labelMax));
	}

	void EndGroupPanel()
	{
		ImGui::PopItemWidth();

		const auto itemSpacing = ImGui::GetStyle().ItemSpacing;
		const auto frameHeight = ImGui::GetFrameHeight();

		ImGui::EndGroup();

		ImGui::EndGroup();

		ImGui::SameLine(0.0f, 0.0f);
		ImGui::Dummy(ImVec2(frameHeight * 0.5f, 0.0f));
		ImGui::Dummy(ImVec2(0.0, frameHeight - frameHeight * 0.5f - itemSpacing.y));

		ImGui::EndGroup();

		auto itemMin = ImGui::GetItemRectMin();
		auto itemMax = ImGui::GetItemRectMax();

		auto labelRect = s_GroupPanelLabelStack.back();
		s_GroupPanelLabelStack.pop_back();

		ImVec2 halfFrame = ImVec2(frameHeight * 0.25f, frameHeight) * 0.5f;
		ImRect frameRect = ImRect(itemMin + halfFrame, itemMax - ImVec2(halfFrame.x, 0.0f));
		labelRect.Min.x -= itemSpacing.x;
		labelRect.Max.x += itemSpacing.x;
		for (int i = 0; i < 4; ++i)
		{
			switch (i)
			{
				// left half-plane
			case 0:
				ImGui::PushClipRect(ImVec2(-FLT_MAX, -FLT_MAX), ImVec2(labelRect.Min.x, FLT_MAX), true);
				break;
				// right half-plane
			case 1:
				ImGui::PushClipRect(ImVec2(labelRect.Max.x, -FLT_MAX), ImVec2(FLT_MAX, FLT_MAX), true);
				break;
				// top
			case 2:
				ImGui::PushClipRect(ImVec2(labelRect.Min.x, -FLT_MAX), ImVec2(labelRect.Max.x, labelRect.Min.y), true);
				break;
				// bottom
			case 3:
				ImGui::PushClipRect(ImVec2(labelRect.Min.x, labelRect.Max.y), ImVec2(labelRect.Max.x, FLT_MAX), true);
				break;
			}

			ImGui::GetWindowDrawList()->AddRect(
				frameRect.Min, frameRect.Max,
				ImColor(ImGui::GetStyleColorVec4(ImGuiCol_Border)),
				halfFrame.x);

			ImGui::PopClipRect();
		}

#if IMGUI_VERSION_NUM >= 17301
		ImGui::GetCurrentWindow()->ContentRegionRect.Max.x += frameHeight * 0.5f;
		ImGui::GetCurrentWindow()->WorkRect.Max.x += frameHeight * 0.5f;
		ImGui::GetCurrentWindow()->InnerRect.Max.x += frameHeight * 0.5f;
#else
		ImGui::GetCurrentWindow()->ContentsRegionRect.Max.x += frameHeight * 0.5f;
#endif
		ImGui::GetCurrentWindow()->Size.x += frameHeight;

		ImGui::Dummy(ImVec2(0.0f, 0.0f));

		ImGui::EndGroup();
	}
}

namespace mmo
{
	static const std::vector<String> s_itemQualityStrings = {
		"Poor",
		"Common",
		"Uncommon",
		"Rare",
		"Epic",
		"Legendary"};

	static String s_itemTriggerTypeStrings[] = {
		"On Use",
		"On Equip",
		"Hit Chance"};

	static_assert(std::size(s_itemTriggerTypeStrings) == static_cast<int>(item_spell_trigger::Count_), "ItemTriggerTypeStrings size mismatch");

	static const ImColor s_itemQualityColors[] = {
		ImColor(0.62f, 0.62f, 0.62f),
		ImColor(1.0f, 1.0f, 1.0f),
		ImColor(0.12f, 1.0f, 0.0f),
		ImColor(0.0f, 0.44f, 0.87f),
		ImColor(0.64f, 0.21f, 0.93f),
		ImColor(1.0f, 0.5f, 0.0f)};

	static const std::vector<String> s_itemSubclassConsumableStrings = {
		"Consumable",
		"Potion",
		"Elixir",
		"Flask",
		"Scroll",
		"Food",
		"Item Enhancement",
		"Bandage"};

	static const std::vector<String> s_itemSubclassContainerStrings = {
		"Container"};

	std::vector<String> s_itemClassStrings = {
		"Consumable",
		"Container",
		"Weapon",
		"Gem",
		"Armor",
		"Reagent",
		"Projectile",
		"Trade Goods",
		"Generic",
		"Recipe",
		"Money",
		"Quiver",
		"Quest",
		"Key",
		"Permanent",
		"Junk"};

	std::vector<String> s_itemSubclassWeaponStrings = {
		"One Handed Axe",
		"Two Handed Axe",
		"Bow",
		"Gun",
		"One Handed Mace",
		"Two Handed Mace",
		"Polearm",
		"One Handed Sword",
		"Two Handed Sword",
		"Staff",
		"Fist",
		"Dagger",
		"Thrown",
		"Spear",
		"Cross Bow",
		"Wand",
		"Fishing Pole"};

	std::vector<String> s_itemSubclassArmorStrings = {
		"Misc",
		"Cloth",
		"Leather",
		"Mail",
		"Plate",
		"Buckler",
		"Shield",
		"Libram",
		"Idol",
		"Totem"};

	static const std::vector<String> s_itemSubclassGemStrings = {
		"Red",
		"Blue",
		"Yellow",
		"Purple",
		"Green",
		"Orange",
		"Prismatic"};

	static const std::vector<String> s_itemSubclassProjectileStrings = {
		"Wand",
		"Bolt",
		"Arrow",
		"Bullet",
		"Thrown"};

	static const std::vector<String> s_itemSubclassTradeGoodsStrings = {
		"TradeGoods",
		"Parts",
		"Eplosives",
		"Devices",
		"Jewelcrafting",
		"Cloth",
		"Leather",
		"MetalStone",
		"Meat",
		"Herb",
		"Elemental",
		"TradeGoodsOther",
		"Enchanting",
		"Material",
	};

	static const std::vector<String> s_inventoryTypeStrings = {
		"NonEquip",
		"Head",
		"Neck",
		"Shoulders",
		"Body",
		"Chest",
		"Waist",
		"Legs",
		"Feet",
		"Wrists",
		"Hands",
		"Finger",
		"Trinket",
		"Weapon",
		"Shield",
		"Ranged",
		"Cloak",
		"Two Handed Weapon",
		"Bag",
		"Tabard",
		"Robe",
		"Main Hand Weapon",
		"Off Hand Weapon",
		"Holdable",
		"Ammo",
		"Thrown",
		"Ranged Right",
		"Quiver",
		"Relic"};

	static const char *s_statTypeStrings[] = {
		"Mana",
		"Health",
		"Agility",
		"Strength",
		"Intellect",
		"Spirit",
		"Stamina",
		"DefenseSkillRating",
		"DodgeRating",
		"ParryRating",
		"BlockRating",
		"HitMeleeRating",
		"HitRangedRating",
		"HitSpellRating",
		"CritMeleeRating",
		"CritRangedRating",
		"CritSpellRating",
		"HitTakenMeleeRating",
		"HitTakenRangedRating",
		"HitTakenSpellRating",
		"CritTakenMeleeRating",
		"CritTakenRangedRating",
		"CritTakenSpellRating",
		"HasteMeleeRating",
		"HasteRangedRating",
		"HasteSpellRating",
		"HitRating",
		"CritRating",
		"HitTakenRating",
		"CritTakenRating",
		"HasteRating",
		"ExpertiseRating"};

	ItemEditorWindow::ItemEditorWindow(const String &name, proto::Project &project, EditorHost &host)
		: EditorEntryWindowBase(project, project.items, name), m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Items";

		std::vector<std::string> files = AssetRegistry::ListFiles();
		for (const auto &filename : files)
		{
			if (filename.ends_with(".htex") && filename.starts_with("Interface/Icon"))
			{
				m_textures.push_back(filename);
			}
		}
	}

	void ItemEditorWindow::OnNewEntry(EntryType &entry)
	{
		EditorEntryWindowBase::OnNewEntry(entry);

		// Stack count starts at 1
		entry.set_maxstack(1);
	}

	void ItemEditorWindow::DrawDetailsImpl(EntryType &currentEntry)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
		if (ImGui::Button("Duplicate Item", ImVec2(140, 0)))
		{
			proto::ItemEntry *copied = m_project.items.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
		}
		ImGui::PopStyleColor();
		ImGui::SameLine();
		DrawHelpMarker("Create a copy of this item with a new ID");

#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max)                               \
	{                                                                                       \
		const char *format = "%d";                                                          \
		uint##datasize value = currentEntry.name();                                         \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{                                                                                   \
			if (value >= min && value <= max)                                               \
				currentEntry.set_##name(value);                                             \
		}                                                                                   \
	}
#define SLIDER_UNSIGNED_SUB_PROP(sub, name, label, datasize, min, max)                      \
	{                                                                                       \
		const char *format = "%d";                                                          \
		uint##datasize value = currentEntry.sub.name();                                     \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{                                                                                   \
			if (value >= min && value <= max)                                               \
				currentEntry.sub.set_##name(value);                                         \
		}                                                                                   \
	}
#define CHECKBOX_BOOL_PROP(name, label)     \
	{                                       \
		bool value = currentEntry.name();   \
		if (ImGui::Checkbox(label, &value)) \
		{                                   \
			currentEntry.set_##name(value); \
		}                                   \
	}
#define CHECKBOX_FLAG_PROP(property, label, flags)                                                  \
	{                                                                                               \
		bool value = (currentEntry.property() & static_cast<uint32>(flags)) != 0;                   \
		if (ImGui::Checkbox(label, &value))                                                         \
		{                                                                                           \
			if (value)                                                                              \
				currentEntry.set_##property(currentEntry.property() | static_cast<uint32>(flags));  \
			else                                                                                    \
				currentEntry.set_##property(currentEntry.property() & ~static_cast<uint32>(flags)); \
		}                                                                                           \
	}
#define CHECKBOX_ATTR_PROP(index, label, flags)                                                                              \
	{                                                                                                                        \
		bool value = (currentEntry.attributes(index) & static_cast<uint32>(flags)) != 0;                                     \
		if (ImGui::Checkbox(label, &value))                                                                                  \
		{                                                                                                                    \
			if (value)                                                                                                       \
				currentEntry.mutable_attributes()->Set(index, currentEntry.attributes(index) | static_cast<uint32>(flags));  \
			else                                                                                                             \
				currentEntry.mutable_attributes()->Set(index, currentEntry.attributes(index) & ~static_cast<uint32>(flags)); \
		}                                                                                                                    \
	}
#define SLIDER_FLOAT_PROP(name, label, min, max)                                      \
	{                                                                                 \
		const char *format = "%.2f";                                                  \
		float value = currentEntry.name();                                            \
		if (ImGui::InputScalar(label, ImGuiDataType_Float, &value, nullptr, nullptr)) \
		{                                                                             \
			if (value >= min && value <= max)                                         \
				currentEntry.set_##name(value);                                       \
		}                                                                             \
	}
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

#define MONEY_PROP_LABEL(name)                                                  \
	{                                                                           \
		const int32 gold = ::floor(currentEntry.name() / 10000);                \
		const int32 silver = ::floor(::fmod(currentEntry.name(), 10000) / 100); \
		const int32 copper = ::fmod(currentEntry.name(), 100);                  \
		if (gold > 0)                                                           \
		{                                                                       \
			ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.0f, 1.0f), "%d g", gold);  \
			ImGui::SameLine();                                                  \
		}                                                                       \
		if (silver > 0 || gold > 0)                                             \
		{                                                                       \
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%d s", silver); \
			ImGui::SameLine();                                                  \
		}                                                                       \
		ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.0f, 1.0f), "%d c", copper);     \
	}

		// Migration for broken maxstack
		if (currentEntry.maxstack() == 0)
		{
			currentEntry.set_maxstack(1);
		}

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			if (ImGui::BeginTable("table", 2, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentEntry.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}

				ImGui::EndTable();
			}

			ImGui::InputText("Description", currentEntry.mutable_description());

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Stack & Inventory");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(maxcount, "Max Count", 0, 255);
			ImGui::SameLine();
			DrawHelpMarker("Maximum number of this item a player can have. 0 = unlimited");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(maxstack, "Max Stack", 1, 255);

			if (currentEntry.itemclass() == ItemClass::Container)
			{
				// Ensure container slot count is clamped
				if (currentEntry.containerslots() < 1 || currentEntry.containerslots() > 36)
				{
					currentEntry.set_containerslots(std::min<uint32>(std::max<uint32>(1, currentEntry.containerslots()), 36));
				}

				SLIDER_UINT32_PROP(containerslots, "Slot Count", 1, 36);
			}

			// Class
			int currentItemClass = currentEntry.itemclass();
			if (ImGui::Combo("Class", &currentItemClass, [](void *, int idx, const char **out_text)
							 {
					if (idx < 0 || idx >= s_itemClassStrings.size())
					{
						return false;
					}

					*out_text = s_itemClassStrings[idx].c_str();
					return true; }, nullptr, s_itemClassStrings.size(), -1))
			{
				currentEntry.set_itemclass(currentItemClass);
			}

			// Subclass
			const std::vector<String> *subclassStrings = nullptr;
			bool hasInventoryType = false;
			switch (currentItemClass)
			{
			case ItemClass::Consumable:
				subclassStrings = &s_itemSubclassConsumableStrings;
				break;
			case ItemClass::Weapon:
				subclassStrings = &s_itemSubclassWeaponStrings;
				hasInventoryType = true;
				break;
			case ItemClass::Armor:
				subclassStrings = &s_itemSubclassArmorStrings;
				hasInventoryType = true;
				break;
			case ItemClass::Container:
				subclassStrings = &s_itemSubclassContainerStrings;
				hasInventoryType = true;
				break;
			case ItemClass::Gem:
				subclassStrings = &s_itemSubclassGemStrings;
				break;
			case ItemClass::Reagent:
				break;
			case ItemClass::Projectile:
				subclassStrings = &s_itemSubclassProjectileStrings;
				break;
			case ItemClass::TradeGoods:
				subclassStrings = &s_itemSubclassTradeGoodsStrings;
				break;
			case ItemClass::Generic:
				break;
			case ItemClass::Recipe:
				break;
			case ItemClass::Money:
				break;
			case ItemClass::Quiver:
				break;
			case ItemClass::Quest:
				break;
			case ItemClass::Key:
				break;
			case ItemClass::Permanent:
				break;
			case ItemClass::Junk:
				break;
			default:
				break;
			}

			if (subclassStrings)
			{
				int currentSubclass = currentEntry.subclass();
				if (ImGui::Combo("Subclass", &currentSubclass, [](void *texts, int idx, const char **out_text)
								 {
						const std::vector<String>* strings = static_cast<std::vector<String>*>(texts);
						if (idx < 0 || idx >= strings->size())
						{
							return false;
						}

						*out_text = (*strings)[idx].c_str();
						return true; }, (void *)subclassStrings, subclassStrings->size(), -1))
				{
					currentEntry.set_subclass(currentSubclass);
				}
			}

			// Inventory Type
			if (hasInventoryType)
			{
				int inventoryType = currentEntry.inventorytype();
				if (ImGui::Combo("Inventory Type", &inventoryType, [](void *, int idx, const char **out_text)
								 {
						if (idx < 0 || idx >= s_inventoryTypeStrings.size())
						{
							return false;
						}

						*out_text = s_inventoryTypeStrings[idx].c_str();
						return true; }, nullptr, s_inventoryTypeStrings.size(), -1))
				{
					currentEntry.set_inventorytype(inventoryType);
				}
				ImGui::SameLine();
				DrawHelpMarker("Equipment slot where this item can be equipped");

				ImGui::Spacing();
				DrawSectionHeader("Equipment Settings");

				ImGui::SetNextItemWidth(150);
				SLIDER_UINT32_PROP(durability, "Durability", 0, 200);
				ImGui::SameLine();
				DrawHelpMarker("Maximum durability of this item");

				// Only show weapon properties for weapons
				if (currentItemClass == item_class::Weapon)
				{
					ImGui::Spacing();
					DrawSectionHeader("Weapon Properties");

					ImGui::SetNextItemWidth(150);
					SLIDER_UINT32_PROP(delay, "Attack Speed (ms)", 0, 100000000);
					ImGui::SameLine();
					DrawHelpMarker("Time between attacks in milliseconds (lower = faster)");

					float damage[2] = {currentEntry.damage().mindmg(), currentEntry.damage().maxdmg()};
					if (ImGui::InputFloat2("Min / Max Damage", damage))
					{
						if (damage[1] < damage[0] || damage[0] > damage[1])
							damage[1] = damage[0];

						currentEntry.mutable_damage()->set_type(0);
						currentEntry.mutable_damage()->set_mindmg(damage[0]);
						currentEntry.mutable_damage()->set_maxdmg(damage[1]);
					}
				}
			}

			ImGui::Spacing();
			DrawSectionHeader("Display Properties");

			// Quality
			int currentQuality = currentEntry.quality();
			if (ImGui::Combo("Quality", &currentQuality, [](void *, int idx, const char **out_text)
							 {
					if (idx < 0 || idx >= s_itemQualityStrings.size())
					{
						return false;
					}

					*out_text = s_itemQualityStrings[idx].c_str();
					return true; }, nullptr, s_itemQualityStrings.size(), -1))
			{
				currentEntry.set_quality(currentQuality);
			}
			ImGui::SameLine();
			DrawHelpMarker("Visual quality of the item (affects text color in tooltip)");

			ImGui::Spacing();
			DrawSectionHeader("Tooltip Preview");

			ImGui::BeginGroupPanel(nullptr, ImVec2(0, -1));
			ImGui::TextColored(s_itemQualityColors[currentQuality].Value, currentEntry.name().c_str());

			if (currentItemClass == item_class::Weapon || currentItemClass == item_class::Armor)
			{
				ImGui::Text("%s", s_inventoryTypeStrings[currentEntry.inventorytype()].c_str());

				if (currentItemClass == item_class::Weapon)
				{
					ImGui::Text("%.0f - %.0f Damage", currentEntry.damage().mindmg(), currentEntry.damage().maxdmg());
					ImGui::SameLine();
					ImGui::Spacing();
					ImGui::SameLine();

					ImGui::Text("Speed %.2f", currentEntry.delay() / 1000.0f);
					ImGui::Text("(%.2f damage per second)", ((currentEntry.damage().maxdmg() - currentEntry.damage().mindmg()) * 0.5f + currentEntry.damage().mindmg()) / (currentEntry.delay() / 1000.0f));
				}
			}
			if (currentEntry.armor() > 0)
			{
				ImGui::Text("%d Armor", currentEntry.armor());
			}
			if (currentEntry.block() > 0)
			{
				ImGui::Text("%d Block", currentEntry.block());
			}
			if (currentEntry.durability() > 0)
			{
				ImGui::Text("Durability %d / %d", currentEntry.durability(), currentEntry.durability());
			}

			// Stats
			for (int i = 0; i < currentEntry.stats_size(); ++i)
			{
				if (currentEntry.stats(i).value() > 0)
				{
					ImGui::TextColored(ImColor(0.0f, 1.0f, 0.0f, 1.0f), "+%d %s", currentEntry.stats(i).value(), s_statTypeStrings[currentEntry.stats(i).type()]);
				}
				else if (currentEntry.stats(i).value() < 0)
				{
					ImGui::TextColored(ImColor(1.0f, 0.0f, 0.0f, 1.0f), "-%d %s", currentEntry.stats(i).value(), s_statTypeStrings[currentEntry.stats(i).type()]);
				}
			}

			if (!currentEntry.description().empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.0f, 1.0f));
				ImGui::TextWrapped("\"%s\"", currentEntry.description().c_str());
				ImGui::PopStyleColor();
			}

			if (currentEntry.sellprice() > 0)
			{
				ImGui::Text("Sell Price: ");
				ImGui::SameLine();
				MONEY_PROP_LABEL(sellprice);
			}
			ImGui::EndGroupPanel();

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		// Equippable items can have stats and spells
		if (currentEntry.itemclass() == ItemClass::Armor || currentEntry.itemclass() == ItemClass::Weapon)
		{
			if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_None))
			{
				ImGui::Indent();
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

				DrawSectionHeader("Armor & Defense");

				ImGui::SetNextItemWidth(150);
				SLIDER_UINT32_PROP(armor, "Armor", 0, 100000000);
				ImGui::SameLine();
				DrawHelpMarker("Armor value provided by this item");

				if (currentEntry.itemclass() == ItemClass::Armor && currentEntry.subclass() == ItemSubclassArmor::Shield)
				{
					ImGui::SetNextItemWidth(150);
					SLIDER_UINT32_PROP(block, "Block", 0, 100000000);
					ImGui::SameLine();
					DrawHelpMarker("Amount of damage blocked by this shield");
				}

				ImGui::BeginDisabled(currentEntry.stats_size() >= 10);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
				if (ImGui::Button("Add Stat", ImVec2(100, 0)))
				{
					currentEntry.add_stats()->set_type(0);
				}
				ImGui::PopStyleColor();
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
				if (ImGui::Button("Remove All", ImVec2(100, 0)))
				{
					currentEntry.clear_stats();
				}
				ImGui::PopStyleColor();
				ImGui::EndDisabled();

				ImGui::SameLine();
				if (currentEntry.stats_size() >= 10)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
					ImGui::TextWrapped("Maximum of 10 stat bonuses reached");
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::TextDisabled("%d / 10 stat bonuses", currentEntry.stats_size());
				}

				ImGui::Spacing();

				if (currentEntry.stats_size() == 0)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
					ImGui::TextWrapped("No stat bonuses defined. Click 'Add Stat' to create one.");
					ImGui::PopStyleColor();
				}
				else if (ImGui::BeginTable("statsTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
				{
					ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableHeadersRow();

					for (int index = 0; index < currentEntry.stats_size(); ++index)
					{
						auto *stat = currentEntry.mutable_stats(index);

						ImGui::PushID(index);
						ImGui::TableNextRow();

						ImGui::TableNextColumn();

						int32 statType = stat->type();
						if (ImGui::BeginCombo("Stat Type", statType < 0 ? "None" : s_statTypeStrings[statType], ImGuiComboFlags_None))
						{
							// Remove the stat entry
							for (int32 j = 0; j < std::size(s_statTypeStrings); ++j)
							{
								if (ImGui::Selectable(s_statTypeStrings[j], statType == j))
								{
									stat->set_type(j);
								}
							}

							ImGui::EndCombo();
						}

						ImGui::TableNextColumn();

						int value = stat->value();
						if (ImGui::InputInt("##value", &value))
						{
							stat->set_value(value);
						}

						ImGui::PopID();
					}

					ImGui::EndTable();
				}

				ImGui::PopStyleVar(2);
				ImGui::Unindent();
			}
		}

		if (ImGui::CollapsingHeader("Spells", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			ImGui::Spacing();
			DrawSectionHeader("Item Spells");

			ImGui::BeginDisabled(currentEntry.spells_size() >= 5);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			if (ImGui::Button("Add Spell", ImVec2(100, 0)))
			{
				currentEntry.add_spells()->set_spell(0);
			}
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
			if (ImGui::Button("Remove All", ImVec2(100, 0)))
			{
				currentEntry.clear_spells();
			}
			ImGui::PopStyleColor();
			ImGui::EndDisabled();

			ImGui::SameLine();
			if (currentEntry.spells_size() >= 5)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
				ImGui::TextWrapped("Maximum of 5 spell effects reached");
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::TextDisabled("%d / 5 spell effects", currentEntry.spells_size());
			}

			ImGui::Spacing();

			if (currentEntry.spells_size() == 0)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
				ImGui::TextWrapped("No spell effects defined. Click 'Add Spell' to create one.");
				ImGui::PopStyleColor();
			}
			else
			{
				static const char *s_spellNone = "<None>";

				if (ImGui::BeginTable("spellsTabel", 7, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
				{
					ImGui::TableSetupColumn("Spell", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Trigger", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Charges", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Proc Rate", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Cooldown", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Category Cooldown", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableHeadersRow();

					for (int index = 0; index < currentEntry.spells_size(); ++index)
					{
						auto *mutableSpellEntry = currentEntry.mutable_spells(index);

						ImGui::PushID(index);
						ImGui::TableNextRow();

						ImGui::TableNextColumn();

						int32 spell = mutableSpellEntry->spell();

						const auto *spellEntry = m_project.spells.getById(spell);
						if (ImGui::BeginCombo("##spell", spellEntry != nullptr ? spellEntry->name().c_str() : s_spellNone, ImGuiComboFlags_None))
						{
							for (int i = 0; i < m_project.spells.count(); i++)
							{
								ImGui::PushID(i);
								const bool item_selected = m_project.spells.getTemplates().entry(i).id() == spell;
								const char *item_text = m_project.spells.getTemplates().entry(i).name().c_str();
								if (ImGui::Selectable(item_text, item_selected))
								{
									mutableSpellEntry->set_spell(m_project.spells.getTemplates().entry(i).id());
								}
								if (item_selected)
								{
									ImGui::SetItemDefaultFocus();
								}
								ImGui::PopID();
							}

							ImGui::EndCombo();
						}

						ImGui::TableNextColumn();

						int triggerType = mutableSpellEntry->trigger();
						if (ImGui::Combo("##triggerType", &triggerType, [](void *data, int idx, const char **out_text)
										 {
							if (idx < 0 || idx >= IM_ARRAYSIZE(s_itemTriggerTypeStrings))
							{
								return false;
							}

							*out_text = s_itemTriggerTypeStrings[idx].c_str();
							return true; }, nullptr, IM_ARRAYSIZE(s_itemTriggerTypeStrings)))
						{
							mutableSpellEntry->set_trigger(triggerType);
						}

						ImGui::TableNextColumn();

						int charges = mutableSpellEntry->charges();
						if (ImGui::InputInt("##charges", &charges))
						{
							mutableSpellEntry->set_charges(charges);
						}

						ImGui::TableNextColumn();

						float procRate = mutableSpellEntry->procrate();
						if (ImGui::InputFloat("%##procRate", &procRate))
						{
							mutableSpellEntry->set_procrate(procRate);
						}

						ImGui::TableNextColumn();

						int cooldown = mutableSpellEntry->cooldown();
						if (ImGui::InputInt("##cooldown", &cooldown))
						{
							mutableSpellEntry->set_cooldown(cooldown);
						}

						ImGui::TableNextColumn();

						int category = mutableSpellEntry->category();
						if (ImGui::InputInt("##category", &category))
						{
							mutableSpellEntry->set_category(category);
						}

						ImGui::TableNextColumn();

						int categoryCooldown = mutableSpellEntry->categorycooldown();
						if (ImGui::InputInt("##categoryCooldown", &categoryCooldown))
						{
							mutableSpellEntry->set_categorycooldown(categoryCooldown);
						}

						ImGui::PopID();
					}

					ImGui::EndTable();
				}
			}

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Vendor", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Vendor Settings");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(buycount, "Buy Count", 0, 100000000);
			ImGui::SameLine();
			DrawHelpMarker("When buying from a vendor, you are forced to buy this many items at a time");

			ImGui::Spacing();

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(buyprice, "Buy Price", 0, 100000000);
			ImGui::SameLine();
			MONEY_PROP_LABEL(buyprice);
			ImGui::SameLine(0, 20);
			DrawHelpMarker("Price when buying from vendor (in copper)");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(sellprice, "Sell Price", 0, 100000000);
			ImGui::SameLine();
			MONEY_PROP_LABEL(sellprice);
			ImGui::SameLine(0, 20);
			DrawHelpMarker("Price when selling to vendor (in copper)");

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		static bool s_spellClientVisible = false;
		if (ImGui::CollapsingHeader("Client Only", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Client Display Settings");

			// Add a dropdown to select a displayDataEntry from m_project.itemDisplays
			int displayId = currentEntry.displayid();
			const auto *displayEntry = m_project.itemDisplays.getById(displayId);
			if (ImGui::BeginCombo("Display ID", displayEntry ? displayEntry->name().c_str() : "(None)"))
			{
				for (int i = 0; i < m_project.itemDisplays.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.itemDisplays.getTemplates().entry(i).id() == displayId;
					const char *item_text = m_project.itemDisplays.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_displayid(m_project.itemDisplays.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			DrawHelpMarker("Visual appearance configuration for client rendering");

			// Edit Display button - opens the item display editor with this display selected
			ImGui::BeginDisabled(displayId == 0);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
			if (ImGui::Button("Edit Display", ImVec2(120, 0)))
			{
				// Find and open the item display editor window
				for (size_t i = 0; i < m_host.GetWindowCount(); ++i)
				{
					EditorWindowBase* window = m_host.GetWindow(i);
					if (window && window->GetName() == "Item Display Editor")
					{
						auto* displayEditor = dynamic_cast<ItemDisplayEditorWindow*>(window);
						if (displayEditor)
						{
							displayEditor->SelectEntryById(displayId);
						}
						break;
					}
				}
			}
			ImGui::PopStyleColor();
			ImGui::EndDisabled();
			ImGui::SameLine();
			DrawHelpMarker("Open the Item Display Editor to edit this item's visual appearance");

			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			if (ImGui::Button("Create New Display Id", ImVec2(180, 0)))
			{
				proto::ItemDisplayEntry *newDisplay = m_project.itemDisplays.add();
				const uint32 newId = newDisplay->id();
				newDisplay->set_name(currentEntry.name());
				newDisplay->set_id(newId);
				currentEntry.set_displayid(newId);
				
				// Automatically open the item display editor with the newly created entry
				for (size_t i = 0; i < m_host.GetWindowCount(); ++i)
				{
					EditorWindowBase* window = m_host.GetWindow(i);
					if (window && window->GetName() == "Item Display Editor")
					{
						auto* displayEditor = dynamic_cast<ItemDisplayEditorWindow*>(window);
						if (displayEditor)
						{
							displayEditor->SelectEntryById(newId);
						}
						break;
					}
				}
			}
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DrawHelpMarker("Create a new display configuration for this item's visual appearance");

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}
	}
}
