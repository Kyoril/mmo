// Copyright (C) 2021, Robin Klimonow. All rights reserved.

#include "worlds_window.h"
#include "data/project.h"
#include <sstream>

#include "imgui_internal.h"

namespace mmo
{
	WorldsWindow::WorldsWindow(Project& project)
		: m_project(project)
        , m_visible(false)
	{
	}

	bool WorldsWindow::Draw()
	{
		// Anything to draw at all?
		if (!m_visible)
			return false;

		// Add the viewport
		if (ImGui::Begin("Worlds", &m_visible))
		{
            // Left
            static int selected = 0;
            {
                
                ImGui::BeginGroup();
                
                m_worldsFilter.Draw("", 250.0f);

                ImGui::BeginChild("left_world_pane", ImVec2(250, -ImGui::GetFrameHeightWithSpacing() * 2), true);

                for (auto& map : m_project.maps.GetTemplates().entry())
                {
                    const char* name = map.name().c_str();
                    if (m_worldsFilter.PassFilter(name))
                    {
                        if (ImGui::Selectable(name, true))
                        {
                            // TODO: Select
                        }
                    }
                }

                ImGui::EndChild();

                if (ImGui::Button("New World", ImVec2(250, 0)))
                {
                    // TODO: Add a new map
                }

                /*TODO: If map selected... */ ImGui::PushDisabled();
                if (ImGui::Button("Delete World", ImVec2(250, 0)))
                {
                    // TODO: Delete selected map and unselect
                }
                /*TODO: If map selected... */ ImGui::PopDisabled();

                ImGui::EndGroup();
            }
            ImGui::SameLine();

            // Right
            {
                ImGui::BeginChild("world_detail_view", ImVec2(0, -ImGui::GetFrameHeightWithSpacing())); // Leave room for 1 line below us
                {
                    ImGui::LabelText("ID", "%d", 1);
                    ImGui::LabelText("Name", "New Map");
                }
                ImGui::EndChild();
            }
		}
		ImGui::End();

		return false;
	}

	bool WorldsWindow::DrawViewMenuItem()
	{
		if (ImGui::MenuItem("Worlds", nullptr, &m_visible)) Show();

		return false;
	}
}
