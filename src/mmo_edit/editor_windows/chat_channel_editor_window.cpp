// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chat_channel_editor_window.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "game/chat_channel_flags.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ChatChannelEditorWindow::ChatChannelEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.chatChannels, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Chat Channels";
	}

	void ChatChannelEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
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

			ImGui::TextDisabled("The Name is also used as the localization key shown to players.");
		}

		if (const auto section = ScopedEditorSection("Flags", ImGuiTreeNodeFlags_DefaultOpen))
		{
			uint32 flags = currentEntry.has_flags() ? currentEntry.flags() : 0;

			bool joinByDefault = (flags & chat_channel_flags::JoinByDefault) != 0;
			if (ImGui::Checkbox("Joined by default", &joinByDefault))
			{
				if (joinByDefault)
				{
					flags |= chat_channel_flags::JoinByDefault;
				}
				else
				{
					flags &= ~static_cast<uint32>(chat_channel_flags::JoinByDefault);
				}
				currentEntry.set_flags(flags);
			}
			ImGui::SameLine();
			DrawHelpMarker("New characters join this channel automatically, and existing characters join it on their next login unless they have explicitly left it.");

			bool moderated = (flags & chat_channel_flags::Moderated) != 0;
			if (ImGui::Checkbox("Moderated", &moderated))
			{
				if (moderated)
				{
					flags |= chat_channel_flags::Moderated;
				}
				else
				{
					flags &= ~static_cast<uint32>(chat_channel_flags::Moderated);
				}
				currentEntry.set_flags(flags);
			}
		}
	}

	void ChatChannelEditorWindow::OnNewEntry(proto::TemplateManager<proto::ChatChannels, proto::ChatChannelEntry>::EntryType& entry)
	{
		entry.set_name("New Channel");
		entry.set_flags(0);
	}
}
