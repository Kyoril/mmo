// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "object_display_editor_window.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ObjectDisplayEditorWindow::ObjectDisplayEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.objectDisplays, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Object Displays";
	}

	void ObjectDisplayEditorWindow::OnNewEntry(EntryType& entry)
	{
		EditorEntryWindowBase::OnNewEntry(entry);


	}

	void ObjectDisplayEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		if (ImGui::Button("Duplicate Display Data"))
		{
			proto::ObjectDisplayEntry* copied = m_project.objectDisplays.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
		}

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
#define SLIDER_UNSIGNED_SUB_PROP(sub, name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry.sub.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.sub.set_##name(value); \
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

		if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
		{
			String filename = currentEntry.filename();
			if (ImGui::InputText("Filename", &filename, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				currentEntry.set_filename(filename);
			}

			// Add drag & drop support for hmsh files
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
				{
					currentEntry.set_filename(*static_cast<String*>(payload->Data));
				}
				ImGui::EndDragDropTarget();
			}

			// Check if file exists
			if (!currentEntry.filename().empty() && !AssetRegistry::HasFile(currentEntry.filename()))
			{
				// Draw warning text that the file does not exist!
				ImGui::TextColored(ImVec4(1, 0, 0, 1), "File does not exist!");
			}
		}
	}
}
