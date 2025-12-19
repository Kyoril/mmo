// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editor_window_base.h"
#include "proto_data/proto_template.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "proto_data/project.h"

#include "imgui_listbox.h"

namespace mmo
{
	/// @brief Base class of a dockable editor ui window.
	template <class T1, class T2>
	class EditorEntryWindowBase : public EditorWindowBase
	{
	public:
		typedef T1 ArrayType;
		typedef T2 EntryType;

	public:
		explicit EditorEntryWindowBase(proto::Project &project, proto::TemplateManager<T1, T2> &manager, const String &name);
		virtual ~EditorEntryWindowBase() = default;

	public:
		/// @brief Called when it's time to draw the window.
		virtual bool Draw() override;

		virtual void DrawDetailsImpl(T2 &currentEntry) = 0;

	protected:
		virtual void OnNewEntry(proto::TemplateManager<T1, T2>::EntryType &entry) {}

		virtual const String &EntryDisplayName(const T2 &entry) { return entry.name(); }

	protected:
		proto::Project &m_project;
		proto::TemplateManager<T1, T2> &m_manager;
	};

	template <class T1, class T2>
	EditorEntryWindowBase<T1, T2>::EditorEntryWindowBase(proto::Project &project, proto::TemplateManager<T1, T2> &manager, const String &name)
		: EditorWindowBase(name), m_project(project), m_manager(manager)
	{
		m_hasToolbarButton = false;
	}

	template <class T1, class T2>
	bool EditorEntryWindowBase<T1, T2>::Draw()
	{
		if (ImGui::Begin(m_name.c_str(), &m_visible))
		{
			ImGui::Columns(2, nullptr, true);
			static bool widthSet = false;
			if (!widthSet)
			{
				ImGui::SetColumnWidth(ImGui::GetColumnIndex(), 380.0f);
				widthSet = true;
			}

			static int currentItem = -1;

			// First column - Entry list with controls
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
				if (ImGui::Button("+ Add New", ImVec2(-1, 0)))
				{
					auto *entry = m_manager.add();
					OnNewEntry(*entry);
					// Select the newly added entry
					currentItem = m_manager.count() - 1;
				}
				ImGui::PopStyleColor();

				ImGui::BeginDisabled(currentItem == -1 || currentItem >= m_project.spells.count());
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
				if (ImGui::Button("Remove Selected", ImVec2(-1, 0)))
				{
					m_manager.remove(m_manager.getTemplates().entry().at(currentItem).id());
					currentItem = -1;
				}
				ImGui::PopStyleColor();
				ImGui::EndDisabled();

				ImGui::Spacing();

				// Search bar with clear button
				static char searchBuffer[256] = "";
				static String lastSearchText = "";

				ImGui::SetNextItemWidth(-38);
				bool searchChanged = ImGui::InputTextWithHint("##search", "Search by name or ID...", searchBuffer, IM_ARRAYSIZE(searchBuffer));

				ImGui::SameLine();
				if (strlen(searchBuffer) > 0)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.3f, 0.8f));
					if (ImGui::Button("X", ImVec2(30, 0)))
					{
						searchBuffer[0] = '\0';
						searchChanged = true;
					}
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::BeginDisabled(true);
					ImGui::Button("X", ImVec2(30, 0));
					ImGui::EndDisabled();
				}

				const String searchText = searchBuffer;
				// Rebuild the filtered list when the search text changes or the entry count changes
				static size_t lastEntryCount = 0;
				const size_t currentEntryCount = m_manager.count();

				// Create a vector of filtered indices for this frame
				std::vector<int> filteredIndices;

				// Only rebuild the list when needed
				if (searchChanged || lastSearchText != searchText || lastEntryCount != currentEntryCount)
				{
					// Update cached values
					lastSearchText = searchText;
					lastEntryCount = currentEntryCount;

					// Convert search text to lowercase for case-insensitive comparison
					std::string lowerSearchText = searchText.c_str();
					std::transform(lowerSearchText.begin(), lowerSearchText.end(), lowerSearchText.begin(),
								   [](unsigned char c)
								   { return std::tolower(c); });

					// Populate filtered indices based on search (name or ID)
					for (int i = 0; i < m_manager.count(); ++i)
					{
						const auto &entry = m_manager.getTemplates().entry().at(i);
						const String &entryName = EntryDisplayName(entry);
						const uint32 entryId = entry.id();

						// Convert entry name to lowercase for case-insensitive comparison
						std::string lowerEntryName = entryName.c_str();
						std::transform(lowerEntryName.begin(), lowerEntryName.end(), lowerEntryName.begin(),
									   [](unsigned char c)
									   { return std::tolower(c); });

						// Also check if search matches ID
						std::string idStr = std::to_string(entryId);

						// If search is empty or entry name/ID contains search text
						if (lowerSearchText.empty() ||
							lowerEntryName.find(lowerSearchText) != std::string::npos ||
							idStr.find(lowerSearchText) != std::string::npos)
						{
							filteredIndices.push_back(i);
						}
					}
				}
				else
				{
					// Reuse the list from last frame if nothing changed
					// Populate filtered indices for the current state
					for (int i = 0; i < m_manager.count(); ++i)
					{
						const auto &entry = m_manager.getTemplates().entry().at(i);
						const String &entryName = EntryDisplayName(entry);
						const uint32 entryId = entry.id();

						// Convert both strings to lowercase for comparison
						std::string lowerEntryName = entryName.c_str();
						std::transform(lowerEntryName.begin(), lowerEntryName.end(), lowerEntryName.begin(),
									   [](unsigned char c)
									   { return std::tolower(c); });

						std::string lowerSearchText = searchText.c_str();
						std::transform(lowerSearchText.begin(), lowerSearchText.end(), lowerSearchText.begin(),
									   [](unsigned char c)
									   { return std::tolower(c); });

						std::string idStr = std::to_string(entryId);

						if (lowerSearchText.empty() ||
							lowerEntryName.find(lowerSearchText) != std::string::npos ||
							idStr.find(lowerSearchText) != std::string::npos)
						{
							filteredIndices.push_back(i);
						}
					}
				}

				// Show entry count
				ImGui::Spacing();
				if (filteredIndices.size() < m_manager.count())
				{
					ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Showing %zu of %zu entries",
									   filteredIndices.size(), m_manager.count());
				}
				else
				{
					ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%zu entries", m_manager.count());
				}
				ImGui::Spacing();

				ImGui::BeginChild("entryListScrollable", ImVec2(-1, 0));

				// Find the filtered index of the currently selected item
				int filteredCurrentItem = -1;
				if (currentItem != -1)
				{
					for (int i = 0; i < filteredIndices.size(); ++i)
					{
						if (filteredIndices[i] == currentItem)
						{
							filteredCurrentItem = i;
							break;
						}
					}
				}

				// Create a persistent pair that we can take the address of
				static std::pair<const T1 *, const std::vector<int> *> listBoxContext;
				listBoxContext.first = &m_manager.getTemplates();
				listBoxContext.second = &filteredIndices;

				// Custom ListBox implementation with ID display
				if (ListBox("##entryList", &filteredCurrentItem, [](void *data, int idx, const char **out_text) -> bool
							{
					auto* context = static_cast<std::pair<const T1*, const std::vector<int>*>*>(data);
					const T1* entries = context->first;
					const std::vector<int>* indices = context->second;
					
					// Map the filtered index to the actual entry index
					int actualIdx = (*indices)[idx];
					const auto& entry = entries->entry().at(actualIdx);
					
					// This is a callback, so we need to use a static buffer
					static char buffer[512];
					const char* displayName = entry.name().c_str();
					const uint32 id = entry.id();
					
					// Format as "ID: Name"
					snprintf(buffer, sizeof(buffer), "%u: %s", id, displayName);
					*out_text = buffer;
					return true; }, &listBoxContext, static_cast<int>(filteredIndices.size()), 20))
				{
					// When selection changes, update the actual selected item
					if (filteredCurrentItem >= 0 && filteredCurrentItem < filteredIndices.size())
					{
						currentItem = filteredIndices[filteredCurrentItem];
					}
					else
					{
						currentItem = -1;
					}
				}

				ImGui::EndChild();
				ImGui::PopStyleVar(2);
			}

			ImGui::NextColumn();

			// Second column - Details view
			{
				T2 *currentEntry = nullptr;
				if (currentItem != -1 && currentItem < m_manager.count())
				{
					currentEntry = &m_manager.getTemplates().mutable_entry()->at(currentItem);
				}

				// Create a child window for the scrollable details
				if (ImGui::BeginChild("entryDetails", ImVec2(-1, 0)))
				{
					if (currentEntry)
					{
						DrawDetailsImpl(*currentEntry);
					}
					else
					{
						ImGui::TextDisabled("No item selected.");
					}
					ImGui::EndChild();
				}
			}

			ImGui::Columns(1);
		}
		ImGui::End();

		return false;
	}
}
