// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "vendor_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/aura.h"
#include "game/spell.h"
#include "game_server/spell_cast.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	VendorEditorWindow::VendorEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.vendors, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Vendors";
	}

	void VendorEditorWindow::DrawDetailsImpl(proto::VendorEntry& currentEntry)
	{

#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_BOOL_PROP(name, label) \
	{ \
		bool value = currentEntry.name(); \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_FLAG_PROP(property, label, flags) \
	{ \
		bool value = (currentEntry.property() & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.set_##property(currentEntry.property() | static_cast<uint32>(flags)); \
			else \
				currentEntry.set_##property(currentEntry.property() & ~static_cast<uint32>(flags)); \
		} \
	}
#define CHECKBOX_ATTR_PROP(index, label, flags) \
	{ \
		bool value = (currentEntry.attributes(index) & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.mutable_attributes()->Set(index, currentEntry.attributes(index) | static_cast<uint32>(flags)); \
			else \
				currentEntry.mutable_attributes()->Set(index, currentEntry.attributes(index) & ~static_cast<uint32>(flags)); \
		} \
	}
#define SLIDER_FLOAT_PROP(name, label, min, max) \
	{ \
		const char* format = "%.2f"; \
		float value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_Float, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

#define MONEY_PROP_LABEL(name) \
	{ \
		const int32 gold = ::floor(currentEntry.name() / 10000); \
		const int32 silver = ::floor(::fmod(currentEntry.name(), 10000) / 100);\
		const int32 copper = ::fmod(currentEntry.name(), 100);\
		if (gold > 0) \
		{ \
			ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.0f, 1.0f), "%d g", gold); \
			ImGui::SameLine(); \
		} \
		if (silver > 0 || gold > 0) \
		{ \
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%d s", silver); \
			ImGui::SameLine(); \
		} \
		ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.0f, 1.0f), "%d c", copper); \
	}

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
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
		}

		if (ImGui::CollapsingHeader("Items"))
		{
			static const char* s_itemNone = "<None>";

			// Add button
			if (ImGui::Button("Add", ImVec2(-1, 0)))
			{
				auto* newEntry = currentEntry.add_items();
				newEntry->set_item(0);
				newEntry->set_maxcount(0);
				newEntry->set_extendedcost(0);
				newEntry->set_interval(0);
				newEntry->set_isactive(true);
			}

			if (ImGui::BeginTable("vendorItems", 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Max Count", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Extended Cost", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Interval", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.items_size(); ++index)
				{
					auto* currentItem = currentEntry.mutable_items(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();

					uint32 item = currentItem->item();

					const auto* itemEntry = m_project.items.getById(item);
					if (ImGui::BeginCombo("##item", itemEntry != nullptr ? itemEntry->name().c_str() : s_itemNone, ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.items.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.items.getTemplates().entry(i).id() == item;
							const char* item_text = m_project.items.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentItem->set_item(m_project.items.getTemplates().entry(i).id());
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

					int32 maxcount = currentItem->maxcount();
					if (ImGui::InputInt("##max_count", &maxcount))
					{
						currentItem->set_maxcount(maxcount);
					}

					ImGui::TableNextColumn();

					int32 cost = currentItem->extendedcost();
					if (ImGui::InputInt("##ext_cost", &cost))
					{
						currentItem->set_extendedcost(cost);
					}

					ImGui::TableNextColumn();

					int32 interval = currentItem->interval();
					if (ImGui::InputInt("##interval", &interval))
					{
						currentItem->set_interval(interval);
					}

					ImGui::TableNextColumn();

					bool active = currentItem->isactive();
					if (ImGui::Checkbox("##active", &active))
					{
						currentItem->set_isactive(active);
					}

					ImGui::SameLine();

					if (ImGui::Button("Remove"))
					{
						currentEntry.mutable_items()->erase(currentEntry.mutable_items()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}
	}
}
