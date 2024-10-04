
#include "imgui_listbox.h"

#include <imgui_internal.h>

namespace mmo
{
	bool ListBox(const char* label, int* current_item, std::function<bool(void*, int, const char**)> items_getter, void* data, int items_count, int height_in_items)
	{
		ImGuiContext& g = *GImGui;

		// Calculate size from "height_in_items"
		if (height_in_items < 0)
			height_in_items = ImMin(items_count, 7);
		float height_in_items_f = height_in_items + 0.25f;
		ImVec2 size(-1.0f, -1.0f /*ImFloor(GetTextLineHeightWithSpacing() * height_in_items_f + g.Style.FramePadding.y * 2.0f)*/);

		if (!ImGui::BeginListBox(label, size))
			return false;

		// Assume all items have even height (= 1 line of text). If you need items of different height,
		// you can create a custom version of ListBox() in your code without using the clipper.
		bool value_changed = false;
		ImGuiListClipper clipper;
		clipper.Begin(items_count, ImGui::GetTextLineHeightWithSpacing()); // We know exactly our line height here so we pass it as a minor optimization, but generally you don't need to.
		while (clipper.Step())
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			{
				const char* item_text;
				if (!items_getter(data, i, &item_text))
					item_text = "*Unknown item*";

				ImGui::PushID(i);
				const bool item_selected = (i == *current_item);
				if (ImGui::Selectable(item_text, item_selected))
				{
					*current_item = i;
					value_changed = true;
				}
				if (item_selected)
					ImGui::SetItemDefaultFocus();
				ImGui::PopID();
			}
		ImGui::EndListBox();

		if (value_changed)
			ImGui::MarkItemEdited(g.LastItemData.ID);

		return value_changed;
	}
}
