// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "log_window.h"

#include "log/default_log.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace mmo
{
	LogWindow::LogWindow()
	{
		m_logConnection = mmo::g_DefaultLog.signal().connect([this](const mmo::LogEntry & entry) {
			std::scoped_lock lock{ m_logWindowMutex };
			m_logEntries.push_back(entry);

			m_selectedItem = m_logEntries.size() - 1;
		});
	}

	static ImVec4 LogLevelColor(mmo::LogColor color)
	{
		ImVec4 logColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		switch (color)
		{
		case LogColor::Green:
			logColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
			break;
		case LogColor::Black:
			logColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
			break;
		case LogColor::Red:
			logColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
			break;
		case LogColor::Yellow:
			logColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
			break;
		case LogColor::Blue:
			logColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
			break;
		case LogColor::Grey:
			logColor = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
			break;
		case LogColor::Purple:
			logColor = ImVec4(0.5f, 0.0f, 1.0f, 1.0f);
			break;
		}

		return logColor;
	}

	bool LogWindow::Draw()
	{
		// Start the window
		if (m_visible)
		{
			if (ImGui::Begin("Log", &m_visible, ImGuiWindowFlags_None))
			{
				// Draw clear button
				if (ImGui::Button("Clear Log"))
				{
					m_logEntries.clear();
				}

				ImGui::SameLine();

				// Draw text filter
				m_filter.Draw("Filter");

				// Draw the log window contents
				ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
				{
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

					if (m_filter.IsActive())
					{
						for (const auto& entry : m_logEntries)
						{
							const auto& line = entry.message;
							if (m_filter.PassFilter(&line.front(), &line.back() + 1))
							{
								ImGui::PushStyleColor(ImGuiCol_Text, LogLevelColor(entry.level->color));
								ImGui::TextUnformatted(&line.front(), &line.back() + 1);
								ImGui::PopStyleColor();
							}
						}
					}
					else
					{
						ImGuiListClipper clipper;
						clipper.Begin(m_logEntries.size());
						while (clipper.Step())
						{
							for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
							{
								const auto& line = m_logEntries[line_no].message;

								ImGui::PushStyleColor(ImGuiCol_Text, LogLevelColor(m_logEntries[line_no].level->color));
								ImGui::TextUnformatted(&line.front(), &line.back() + 1);
								ImGui::PopStyleColor();
							}
						}
						clipper.End();
					}

					ImGui::PopStyleVar();

					if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
					{
						ImGui::SetScrollHereY(1.0f);
					}
				}
				
				ImGui::EndChild();
			}
			ImGui::End();
		}

		// Result is false in this case because we apply ImGui behavior for render functions
		// where true indicates an event happened.
		return false;
	}

	bool LogWindow::DrawViewMenuItem()
	{
		if (ImGui::MenuItem("Log", nullptr, &m_visible)) Show();

		return false;
	}

	void LogWindow::Show()
	{
		m_visible = true;
	}
}
