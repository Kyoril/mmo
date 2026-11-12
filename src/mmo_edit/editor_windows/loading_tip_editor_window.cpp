// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "loading_tip_editor_window.h"
#include "editor_imgui_helpers.h"

#include "base/localization.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace mmo
{
	LoadingTipEditorWindow::LoadingTipEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.loadingTips, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Loading Tips";
	}

	void LoadingTipEditorWindow::OnNewEntry(proto::TemplateManager<proto::LoadingTips, proto::LoadingTip>::EntryType& entry)
	{
		entry.set_name("New Loading Tip");
	}

	void LoadingTipEditorWindow::DrawDetailsImpl(proto::LoadingTip& currentEntry)
	{
		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
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

			ImGui::TextDisabled("The name is only shown in the editor.");
		}

		const ImVec2 textBoxSize(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f);

		if (const auto section = ScopedEditorSection("Text", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextUnformatted("enUS (base)");
			ImGui::InputTextMultiline("##text_enUS", currentEntry.mutable_text(), textBoxSize);
		}

		if (const auto section = ScopedEditorSection("Translations", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Empty translations fall back to the base text.");

			for (uint32 i = 1; i < static_cast<uint32>(LocaleIndex::Count); ++i)
			{
				const LocaleIndex locale = static_cast<LocaleIndex>(i);

				// Edit a copy so that merely viewing a locale does not add an empty override.
				String value = GetLocalizedString(String(), currentEntry.text_loc(), locale);

				ImGui::PushID(static_cast<int>(i));
				ImGui::TextUnformatted(LocaleCode(locale));
				if (ImGui::InputTextMultiline("##text_loc", &value, textBoxSize))
				{
					FindOrAddLoc(currentEntry.mutable_text_loc(), locale)->set_value(value);
				}
				ImGui::PopID();
			}
		}
	}
}
