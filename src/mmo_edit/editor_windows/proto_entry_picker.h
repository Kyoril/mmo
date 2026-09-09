// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <cstdio>
#include <map>
#include <string>

#include "base/typedefs.h"
#include "editor_imgui_helpers.h"

namespace mmo
{
	/// @brief Draws a combo box over a fixed set of named enum values.
	///
	/// Editors store enum-valued data as plain integers, which leaves the author to remember the
	/// numbering. This renders the names instead and writes the picked index back.
	///
	/// @param id ImGui id of the widget, including the "##" prefix.
	/// @param label Text drawn to the right of the combo box.
	/// @param value In/out enum value. A value outside the name table is shown verbatim rather
	///        than snapped to the first entry, so out-of-range data stays visible.
	/// @param names Display names, indexed by enum value.
	/// @param count Number of entries in @p names.
	/// @param tooltip Optional help marker text, or nullptr for none.
	/// @param width Width of the combo box in pixels.
	/// @return True if the value changed this frame.
	inline bool DrawEnumCombo(const char* id, const char* label, int& value, const char* const* names,
		const int count, const char* tooltip = nullptr, const float width = 220.0f)
	{
		bool changed = false;

		char preview[128];
		if (value >= 0 && value < count)
		{
			snprintf(preview, sizeof(preview), "%s (%d)", names[value], value);
		}
		else
		{
			snprintf(preview, sizeof(preview), "<unknown value %d>", value);
		}

		const bool unknown = (value < 0 || value >= count);
		if (unknown)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
		}

		ImGui::SetNextItemWidth(width);
		const bool open = ImGui::BeginCombo(id, preview);

		if (unknown)
		{
			ImGui::PopStyleColor();
		}

		if (open)
		{
			for (int i = 0; i < count; ++i)
			{
				char entry[128];
				snprintf(entry, sizeof(entry), "%s (%d)", names[i], i);

				if (ImGui::Selectable(entry, i == value))
				{
					value = i;
					changed = true;
				}

				if (i == value)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		ImGui::SameLine();
		ImGui::Text("%s", label);

		if (tooltip != nullptr)
		{
			ImGui::SameLine();
			DrawHelpMarker(tooltip);
		}

		return changed;
	}

	namespace detail
	{
		/// Search text of every open entry picker, keyed by widget id.
		///
		/// The text has to outlive the popup's frame but must not be shared between two pickers
		/// drawn in the same frame, which rules out both a local and a single static buffer.
		/// Entries are erased when their popup closes, so this does not grow with use.
		inline std::map<ImGuiID, std::string>& EntryPickerSearchTerms()
		{
			static std::map<ImGuiID, std::string> terms;
			return terms;
		}
	}

	/// @brief Draws a searchable combo box that picks an entry id out of a proto template manager.
	///
	/// The editor already has every entry's name loaded, so an id field is rendered as its name
	/// with the id in brackets. An id that resolves to no entry is called out in red rather than
	/// rendering blank, which is what made dangling references invisible.
	///
	/// @tparam Manager A proto::TemplateManager specialization; its entry type needs id() and name().
	/// @param id ImGui id of the widget, including the "##" prefix.
	/// @param label Text drawn to the right of the combo box.
	/// @param value In/out entry id. 0 means "none".
	/// @param manager Manager supplying the selectable entries.
	/// @param tooltip Optional help marker text, or nullptr for none.
	/// @param width Width of the combo box in pixels.
	/// @return True if the value changed this frame.
	template <class Manager>
	bool DrawEntryPicker(const char* id, const char* label, int& value, const Manager& manager,
		const char* tooltip = nullptr, const float width = 320.0f)
	{
		bool changed = false;

		const auto* current = (value != 0) ? manager.getById(static_cast<uint32>(value)) : nullptr;
		const bool missing = (value != 0 && current == nullptr);

		char preview[256];
		if (value == 0)
		{
			snprintf(preview, sizeof(preview), "None (0)");
		}
		else if (current != nullptr)
		{
			snprintf(preview, sizeof(preview), "%s (%d)", current->name().c_str(), value);
		}
		else
		{
			snprintf(preview, sizeof(preview), "<missing #%d>", value);
		}

		// Only the preview is tinted; a red popup would make the list itself hard to read.
		if (missing)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
		}

		const ImGuiID widgetId = ImGui::GetID(id);

		ImGui::SetNextItemWidth(width);
		const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge);

		if (missing)
		{
			ImGui::PopStyleColor();
		}

		auto& searchTerms = detail::EntryPickerSearchTerms();

		if (open)
		{
			std::string& search = searchTerms[widgetId];

			// Typing goes straight into the search box: a combo that opens with the keyboard
			// elsewhere is otherwise a list you can only scroll.
			if (ImGui::IsWindowAppearing())
			{
				search.clear();
				ImGui::SetKeyboardFocusHere();
			}

			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##search", "Search...", &search);

			ImGui::Separator();

			if (ImGui::BeginChild("##entries", ImVec2(0.0f, 260.0f)))
			{
				if (search.empty() && ImGui::Selectable("None (0)", value == 0))
				{
					value = 0;
					changed = true;
					ImGui::CloseCurrentPopup();
				}

				for (const auto& entry : manager.getTemplates().entry())
				{
					char text[256];
					snprintf(text, sizeof(text), "%s (%u)", entry.name().c_str(), entry.id());

					// Matching the rendered "name (id)" means an id can be searched for as well
					// as a name, which is how an author cross-references a value they already have.
					if (!search.empty() && !ImStristr(text, nullptr, search.c_str(), nullptr))
					{
						continue;
					}

					if (ImGui::Selectable(text, static_cast<uint32>(value) == entry.id()))
					{
						value = static_cast<int>(entry.id());
						changed = true;
						ImGui::CloseCurrentPopup();
					}
				}
			}
			ImGui::EndChild();

			ImGui::EndCombo();
		}
		else
		{
			searchTerms.erase(widgetId);
		}

		ImGui::SameLine();
		ImGui::Text("%s", label);

		if (tooltip != nullptr)
		{
			ImGui::SameLine();
			DrawHelpMarker(tooltip);
		}

		return changed;
	}
}
