// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/attribute.h"
#include "proto_data/project.h"

#include "editor_imgui_helpers.h"

#include <imgui.h>

#include <array>

namespace mmo
{
	/// @brief Returns the display label of an attribute id: its name, or "Unknown (#id)" if the
	///        project defines no attribute with that id.
	/// @param attributes The project's attribute manager.
	/// @param attributeId The attribute id (slot) to describe.
	/// @returns The label to show in the UI.
	inline String GetAttributeLabel(const proto::AttributeManager& attributes, const uint32 attributeId)
	{
		if (const auto* entry = attributes.getById(attributeId))
		{
			return entry->name();
		}

		return "Unknown (#" + std::to_string(attributeId) + ")";
	}

	/// @brief Returns a stable plot / legend color for an attribute id.
	/// @param attributeId The attribute id (slot).
	/// @returns A color that is the same for the same attribute id in every editor.
	inline ImU32 GetAttributeColor(const uint32 attributeId)
	{
		static const std::array<ImU32, MaxAttributes> s_colors = { {
			IM_COL32(90, 210, 120, 255),
			IM_COL32(255, 160, 70, 255),
			IM_COL32(245, 235, 120, 255),
			IM_COL32(180, 120, 255, 255),
			IM_COL32(120, 235, 235, 255),
			IM_COL32(255, 120, 200, 255),
			IM_COL32(160, 200, 90, 255),
			IM_COL32(200, 160, 120, 255),
			IM_COL32(120, 160, 220, 255),
			IM_COL32(220, 220, 220, 255),
		} };

		return s_colors[attributeId % s_colors.size()];
	}

	/// @brief Returns the id of the first defined attribute in display order, or 0 if none is defined.
	/// @param attributes The project's attribute manager.
	inline uint32 GetFirstAttributeId(const proto::AttributeManager& attributes)
	{
		const auto& entries = attributes.getTemplates().entry();
		return entries.empty() ? 0 : entries.Get(0).id();
	}

	/// @brief Reads an attribute value from a per-attribute array (indexed by attribute id).
	/// @param values The repeated field indexed by attribute id. Missing entries count as 0.
	/// @param attributeId The attribute id to read.
	/// @returns The stored value or 0.
	template<typename TRepeated>
	uint32 GetAttributeValue(const TRepeated& values, const uint32 attributeId)
	{
		return static_cast<int>(attributeId) < values.size() ? values.Get(static_cast<int>(attributeId)) : 0;
	}

	/// @brief Writes an attribute value into a per-attribute array, growing it with zeros so that
	///        the index of the attribute id exists.
	/// @param values The repeated field indexed by attribute id.
	/// @param attributeId The attribute id to write.
	/// @param value The new value.
	template<typename TRepeated>
	void SetAttributeValue(TRepeated& values, const uint32 attributeId, const uint32 value)
	{
		while (values.size() <= static_cast<int>(attributeId))
		{
			values.Add(0);
		}

		values.Set(static_cast<int>(attributeId), value);
	}

	/// @brief Draws a combo box listing every attribute defined in the project.
	/// @param label The ImGui label of the combo.
	/// @param attributes The project's attribute manager.
	/// @param attributeId In/out: the selected attribute id. Ids not defined in the project are shown as "Unknown (#id)".
	/// @returns true if the selection changed.
	inline bool DrawAttributeCombo(const char* label, const proto::AttributeManager& attributes, uint32& attributeId)
	{
		bool changed = false;

		const String preview = GetAttributeLabel(attributes, attributeId);
		if (ImGui::BeginCombo(label, preview.c_str()))
		{
			for (const auto& entry : attributes.getTemplates().entry())
			{
				ImGui::PushID(static_cast<int>(entry.id()));
				const bool selected = entry.id() == attributeId;
				if (ImGui::Selectable(entry.name().c_str(), selected))
				{
					attributeId = entry.id();
					changed = true;
				}

				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}

			if (attributes.getTemplates().entry_size() == 0)
			{
				ImGui::TextDisabled("No attributes defined (see Attribute Editor).");
			}

			ImGui::EndCombo();
		}

		return changed;
	}

	/// @brief Draws an editable list of stat sources which add (attribute value * factor) to a
	///        derived value. Works with every message that has `statid` and `factor` fields.
	/// @param tableId Unique ImGui id of the list.
	/// @param attributes The project's attribute manager.
	/// @param sources The repeated message field to edit.
	template<typename TRepeated>
	void DrawAttributeFactorSourceList(const char* tableId, const proto::AttributeManager& attributes, TRepeated& sources)
	{
		ImGui::PushID(tableId);

		if (DrawSuccessButton("Add", ImVec2(-1, 0)))
		{
			auto* added = sources.Add();
			added->set_statid(GetFirstAttributeId(attributes));
			added->set_factor(1.0f);
		}

		if (ImGui::BeginTable("sources", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableSetupColumn("Attribute", ImGuiTableColumnFlags_DefaultSort);
			ImGui::TableSetupColumn("Factor", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (int index = 0; index < sources.size(); ++index)
			{
				auto* source = sources.Mutable(index);

				ImGui::PushID(index);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				uint32 attributeId = source->statid();
				ImGui::SetNextItemWidth(-1.0f);
				if (DrawAttributeCombo("##attribute", attributes, attributeId))
				{
					source->set_statid(attributeId);
				}

				ImGui::TableNextColumn();
				float factor = source->factor();
				if (ImGui::InputFloat("##factor", &factor))
				{
					source->set_factor(factor);
				}

				ImGui::SameLine();
				if (DrawDangerButton("Remove"))
				{
					sources.erase(sources.begin() + index);
					--index;
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		ImGui::PopID();
	}

	/// @brief Draws an editable list of attribute ratio sources which add (attribute value / pointsPer)
	///        to a derived value. Works with every message that has `attribute` and `pointsper` fields.
	/// @param tableId Unique ImGui id of the list.
	/// @param attributes The project's attribute manager.
	/// @param sources The repeated message field to edit.
	/// @param defaultPointsPer Points per unit used for newly added rows.
	template<typename TRepeated>
	void DrawAttributeRatioSourceList(const char* tableId, const proto::AttributeManager& attributes, TRepeated& sources, const float defaultPointsPer = 20.0f)
	{
		ImGui::PushID(tableId);

		if (DrawSuccessButton("Add", ImVec2(-1, 0)))
		{
			auto* added = sources.Add();
			added->set_attribute(GetFirstAttributeId(attributes));
			added->set_pointsper(defaultPointsPer);
		}

		if (ImGui::BeginTable("sources", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableSetupColumn("Attribute", ImGuiTableColumnFlags_DefaultSort);
			ImGui::TableSetupColumn("Points per 1", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (int index = 0; index < sources.size(); ++index)
			{
				auto* source = sources.Mutable(index);

				ImGui::PushID(index);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				uint32 attributeId = source->attribute();
				ImGui::SetNextItemWidth(-1.0f);
				if (DrawAttributeCombo("##attribute", attributes, attributeId))
				{
					source->set_attribute(attributeId);
				}

				ImGui::TableNextColumn();
				float pointsPer = source->pointsper();
				if (ImGui::InputFloat("##pointsPer", &pointsPer))
				{
					source->set_pointsper(pointsPer);
				}

				if (pointsPer == 0.0f)
				{
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "(0 is ignored)");
				}

				ImGui::SameLine();
				if (DrawDangerButton("Remove"))
				{
					sources.erase(sources.begin() + index);
					--index;
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		ImGui::PopID();
	}
}
