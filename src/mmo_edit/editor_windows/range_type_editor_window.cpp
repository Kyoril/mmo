// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "range_type_editor_window.h"

namespace mmo
{
	RangeTypeEditorWindow::RangeTypeEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.ranges, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);
	}

	void RangeTypeEditorWindow::DrawDetailsImpl(proto::RangeType& currentEntry)
	{
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

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 3, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Internal Name", currentEntry.mutable_internalname());
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

			SLIDER_FLOAT_PROP(range, "Range", 0.0f, 50000.0f);
		}
	}
}
