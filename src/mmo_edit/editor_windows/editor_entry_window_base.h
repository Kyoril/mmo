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
	template<class T1, class T2>
	class EditorEntryWindowBase : public EditorWindowBase
	{
	public:
		typedef T1 ArrayType;
		typedef T2 EntryType;

	public:
		explicit EditorEntryWindowBase(proto::Project& project, proto::TemplateManager<T1, T2>& manager, const String& name);
		virtual ~EditorEntryWindowBase() = default;

	public:
		/// @brief Called when it's time to draw the window.
		virtual bool Draw() override;

		virtual void DrawDetailsImpl(T2& currentEntry) = 0;

	protected:
		virtual void OnNewEntry(proto::TemplateManager<T1, T2>::EntryType& entry) {}

		virtual const String& EntryDisplayName(const T2& entry) { return entry.name(); }

	protected:
		proto::Project& m_project;
		proto::TemplateManager<T1, T2>& m_manager;
	};

	template <class T1, class T2>
	EditorEntryWindowBase<T1, T2>::EditorEntryWindowBase(proto::Project& project, proto::TemplateManager<T1, T2>& manager, const String& name)
		: EditorWindowBase(name)
		, m_project(project)
		, m_manager(manager)
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
				ImGui::SetColumnWidth(ImGui::GetColumnIndex(), 350.0f);
				widthSet = true;
			}

			static int currentItem = -1;

			// First column - Entry list with controls
			{
				if (ImGui::Button("Add New", ImVec2(-1, 0)))
				{
					auto* entry = m_manager.add();
					OnNewEntry(*entry);
				}

				ImGui::BeginDisabled(currentItem == -1 || currentItem >= m_project.spells.count());
				if (ImGui::Button("Remove", ImVec2(-1, 0)))
				{
					m_manager.remove(m_manager.getTemplates().entry().at(currentItem).id());
				}
				ImGui::EndDisabled();

				// Draw a search filter at the top
				static char searchBuffer[256] = "";
				static String lastSearchText = "";
				ImGui::SetNextItemWidth(-1);
				bool searchChanged = ImGui::InputTextWithHint("##search", "Search...", searchBuffer, IM_ARRAYSIZE(searchBuffer));

				const String searchText = searchBuffer;

				// Rebuild the filtered list when the search text changes or the entry count changes
				static size_t lastEntryCount = 0;
				const size_t currentEntryCount = m_manager.count();
				
				// Create a vector of filtered indices for this frame
				std::vector<int> filteredIndices;
				
				// Only rebuild the list when needed
				if (searchChanged || lastSearchText != searchText || lastEntryCount != currentEntryCount) {
					// Update cached values
					lastSearchText = searchText;
					lastEntryCount = currentEntryCount;
					
					// Convert search text to lowercase for case-insensitive comparison
					std::string lowerSearchText = searchText.c_str();
					std::transform(lowerSearchText.begin(), lowerSearchText.end(), lowerSearchText.begin(),
						[](unsigned char c) { return std::tolower(c); });
					
					// Populate filtered indices based on search
					for (int i = 0; i < m_manager.count(); ++i)
					{
						const auto& entry = m_manager.getTemplates().entry().at(i);
						const String& entryName = EntryDisplayName(entry);
						
						// Convert entry name to lowercase for case-insensitive comparison
						std::string lowerEntryName = entryName.c_str();
						std::transform(lowerEntryName.begin(), lowerEntryName.end(), lowerEntryName.begin(),
							[](unsigned char c) { return std::tolower(c); });
						
						// If search is empty or entry name contains search text
						if (lowerSearchText.empty() || lowerEntryName.find(lowerSearchText) != std::string::npos)
						{
							filteredIndices.push_back(i);
						}
					}
				}
				else {
					// Reuse the list from last frame if nothing changed
					// Populate filtered indices for the current state
					for (int i = 0; i < m_manager.count(); ++i)
					{
						const auto& entry = m_manager.getTemplates().entry().at(i);
						const String& entryName = EntryDisplayName(entry);
						
						// Convert both strings to lowercase for comparison
						std::string lowerEntryName = entryName.c_str();
						std::transform(lowerEntryName.begin(), lowerEntryName.end(), lowerEntryName.begin(),
							[](unsigned char c) { return std::tolower(c); });
						
						std::string lowerSearchText = searchText.c_str();
						std::transform(lowerSearchText.begin(), lowerSearchText.end(), lowerSearchText.begin(),
							[](unsigned char c) { return std::tolower(c); });
						
						if (lowerSearchText.empty() || lowerEntryName.find(lowerSearchText) != std::string::npos)
						{
							filteredIndices.push_back(i);
						}
					}
				}
				
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
				static std::pair<const T1*, const std::vector<int>*> listBoxContext;
				listBoxContext.first = &m_manager.getTemplates();
				listBoxContext.second = &filteredIndices;
				
				// Custom ListBox implementation
				if (ListBox("##entryList", &filteredCurrentItem, 
					[](void* data, int idx, const char** out_text) -> bool
					{
						auto* context = static_cast<std::pair<const T1*, const std::vector<int>*>*>(data);
						const T1* entries = context->first;
						const std::vector<int>* indices = context->second;
						
						// Map the filtered index to the actual entry index
						int actualIdx = (*indices)[idx];
						const auto& entry = entries->entry().at(actualIdx);
						
						// This is a callback, so we need to use a static buffer
						static char buffer[256];
						const char* displayName = entry.name().c_str();
						strcpy_s(buffer, displayName);
						*out_text = buffer;
						return true;
					}, 
					&listBoxContext, 
					static_cast<int>(filteredIndices.size()), 20))
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
			}

			ImGui::NextColumn();

			// Second column - Details view
			{
				// Add title to match the height of the buttons in the first column
				ImGui::Text("Details");
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Selected Item").x);
				ImGui::Text("Selected Item");
				
				T2* currentEntry = nullptr;
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
