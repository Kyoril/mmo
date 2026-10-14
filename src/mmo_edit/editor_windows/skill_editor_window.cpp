// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "skill_editor_window.h"
#include "editor_imgui_helpers.h"

#include <algorithm>

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "game/trade_skill.h"
#include "log/default_log_levels.h"

namespace mmo
{
	SkillEditorWindow::SkillEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.skills, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Skills";
	}

	void SkillEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
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

				if (ImGui::TableNextColumn())
				{
					int category = currentEntry.category();
					if (ImGui::InputInt("Category", &category))
					{
						currentEntry.set_category(category);
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("1 = Profession");
					}
				}

				if (ImGui::TableNextColumn())
				{
					uint32 cost = currentEntry.cost();
					if (ImGui::InputScalar("Cost", ImGuiDataType_U32, &cost))
					{
						currentEntry.set_cost(cost);
					}
				}

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Starter Spells", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Button("Add Spell"))
			{
				ImGui::OpenPopup("Select Starter Spell");
			}

			if (ImGui::BeginPopup("Select Starter Spell"))
			{
				static char filter[128] = "";
				ImGui::InputText("Filter", filter, sizeof(filter));
				ImGui::Separator();

				ImGui::BeginChild("starterSpellList", ImVec2(360.0f, 240.0f), true);
				for (const auto& spell : m_project.spells.getTemplates().entry())
				{
					if (filter[0] != '\0' && spell.name().find(filter) == String::npos)
					{
						continue;
					}

					const auto& existing = currentEntry.spells();
					if (std::find(existing.begin(), existing.end(), spell.id()) != existing.end())
					{
						continue;
					}

					ImGui::PushID(static_cast<int>(spell.id()));
					const String label = std::to_string(spell.id()) + " - " + spell.name();
					if (ImGui::Selectable(label.c_str()))
					{
						currentEntry.add_spells(spell.id());
						ImGui::CloseCurrentPopup();
					}
					ImGui::PopID();
				}
				ImGui::EndChild();

				if (ImGui::Button("Close"))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			int removeIndex = -1;
			for (int i = 0; i < currentEntry.spells_size(); ++i)
			{
				ImGui::PushID(i);
				if (const auto* spell = m_project.spells.getById(currentEntry.spells(i)))
				{
					ImGui::Text("%u - %s", spell->id(), spell->name().c_str());
				}
				else
				{
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Unknown Spell (%u)", currentEntry.spells(i));
				}

				ImGui::SameLine();
				if (DrawDangerButton("Remove", ImVec2(80, 0)))
				{
					removeIndex = i;
				}
				ImGui::PopID();
			}

			if (removeIndex >= 0)
			{
				currentEntry.mutable_spells()->erase(currentEntry.mutable_spells()->begin() + removeIndex);
			}

			if (currentEntry.spells_size() == 0)
			{
				ImGui::TextDisabled("No starter spells.");
			}
		}

		if (const auto section = ScopedEditorSection("Categories", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Button("Add Category"))
			{
				uint32 nextId = 1;
				for (const auto& category : currentEntry.categories())
				{
					nextId = std::max(nextId, category.id() + 1);
				}

				auto* category = currentEntry.add_categories();
				category->set_id(nextId);
				category->set_name("New Category");
				category->set_sortorder(nextId * 10);
			}

			if (ImGui::BeginTable("categories", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Sort Order", ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableHeadersRow();

				int removeIndex = -1;
				for (int i = 0; i < currentEntry.categories_size(); ++i)
				{
					ImGui::PushID(i);
					auto* category = currentEntry.mutable_categories(i);
					ImGui::TableNextRow();

					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", category->id());

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::InputText("##name", category->mutable_name());

					ImGui::TableSetColumnIndex(2);
					uint32 sortOrder = category->sortorder();
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::InputScalar("##sort", ImGuiDataType_U32, &sortOrder))
					{
						category->set_sortorder(sortOrder);
					}

					ImGui::TableSetColumnIndex(3);
					if (DrawDangerButton("Remove", ImVec2(-1, 0)))
					{
						removeIndex = i;
					}
					ImGui::PopID();
				}

				ImGui::EndTable();

				if (removeIndex >= 0)
				{
					currentEntry.mutable_categories()->erase(currentEntry.mutable_categories()->begin() + removeIndex);
				}
			}
		}
	}

	void SkillEditorWindow::OnNewEntry(proto::TemplateManager<proto::Skills, proto::SkillEntry>::EntryType& entry)
	{
		entry.set_name("New Skill");
		entry.set_category(skill_category::Profession);
		entry.set_cost(0);
	}
}
