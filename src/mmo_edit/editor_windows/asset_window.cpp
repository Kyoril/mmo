// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "asset_window.h"

#include <imgui.h>

#include "assets/asset_registry.h"
#include "graphics/texture.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	void AssetWindow::RebuildAssetList()
	{
		m_selectedEntry = nullptr;
		m_assets.clear();

		// Gather a list of all assets in the registry
		const auto assets = AssetRegistry::ListFiles();
		for(const auto& asset : assets)
		{
			if (asset.starts_with(".")) continue;

			// Split asset path
			const auto separatorIndex = asset.find('/');
			if (separatorIndex == std::string::npos)
			{
				m_assets[asset] = { asset, {} };
			}
			else
			{
				const auto name = asset.substr(0, separatorIndex);
				const auto remainingPath = asset.substr(separatorIndex + 1);

				auto entryIt = m_assets.find(name);
				if (entryIt == m_assets.end())
				{
					AssetEntry& parent = (m_assets[name] = { name, {} });
					AddAssetToMap(parent, remainingPath);

					if (m_host.GetCurrentPath() == parent.fullPath)
					{
						m_selectedEntry = &parent;
					}
				}
				else
				{
					AddAssetToMap(entryIt->second, remainingPath);
					
					if (m_host.GetCurrentPath() == entryIt->second.fullPath)
					{
						m_selectedEntry = &entryIt->second;
					}
				}
			}
		}
	}

	AssetWindow::AssetWindow(const String& name, PreviewProviderManager& previewProviderManager, EditorHost& host)
		: EditorWindowBase(name)
		, m_previewProviderManager(previewProviderManager)
		, m_host(host)
	{
		m_host.assetImported.connect([&](const Path& p)
		{
			m_selectedEntry = nullptr;
			m_assets.clear();

			RebuildAssetList();
		});

		m_folderTexture = TextureManager::Get().CreateOrRetrieve("Editor/Folder_BaseHi_256x.htex");
		RebuildAssetList();
	}
	
	void AssetWindow::RenderAssetEntry(const std::string& name, const AssetEntry& entry, const std::string& path)
	{
		// If there are no children, we don't need to continue
		if (entry.children.empty())
		{
			return;
		}
		
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
		if (m_selectedEntry == &entry)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}

		// There are children, render them
		if (ImGui::TreeNodeEx(name.c_str(), flags))
		{
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				m_selectedEntry = &entry;
				m_host.SetCurrentPath(entry.fullPath);
			}
			
			const auto childPath = path + "/" + name;
			for(const auto& child : entry.children)
			{
				RenderAssetEntry(child.first, child.second, childPath);
			}

			ImGui::TreePop();
		}
	}

	void AssetWindow::AddAssetToMap(AssetEntry& parent, const std::string& assetPath)
	{
		// Check if we are done already
		const auto separatorIndex = assetPath.find('/');
		if (separatorIndex == std::string::npos)
		{
			// Add the final piece
			parent.children[assetPath] = { parent.fullPath + "/" + assetPath, {}};
			return;
		}

		// Not done yet, as we've found a separator in the path.
		const auto childName = assetPath.substr(0, separatorIndex);
		const auto remainingPath = assetPath.substr(separatorIndex + 1);

		// Find existing child
		const auto childIt = parent.children.find(childName);
		if (childIt == parent.children.end())
		{
			// Child doesn't yet exist, so add it
			AssetEntry& child = (parent.children[childName] = { parent.fullPath + "/" + childName, {} });
			AddAssetToMap(child, remainingPath);
		}
		else
		{
			AddAssetToMap(childIt->second, remainingPath);
		}
	}

	bool AssetWindow::Draw()
	{
		if (ImGui::Begin(m_name.c_str(), &m_visible))
		{
			// Breadcrumb navigation menu at the top of the entire window
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.12f, 1.0f)); // Dark background for the breadcrumb bar
			ImGui::BeginChild("BreadcrumbBar", ImVec2(ImGui::GetContentRegionAvail().x, 36), false, 
			                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			
			// Center the breadcrumb contents vertically
			ImGui::SetCursorPosY((36 - ImGui::GetTextLineHeightWithSpacing()) * 0.5f);
			
			// Add some left padding for aesthetics
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
			
			// Set up the style for breadcrumb buttons
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.25f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.45f, 1.0f));
			
			// Root button - always displayed
			if (m_selectedEntry == nullptr)
			{
				// We're already at root, display as selected
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.45f, 0.8f));
				ImGui::Button("Root");
				ImGui::PopStyleColor();
			}
			else
			{
				// Regular root button that returns to root when clicked
				if (ImGui::Button("Root"))
				{
					m_selectedEntry = nullptr;
					m_host.SetCurrentPath("");
				}
				
				// Display path components as a breadcrumb when we're not at root
				if (!m_selectedEntry->fullPath.empty())
				{
					std::string fullPath = m_selectedEntry->fullPath;
					std::vector<std::string> pathComponents;
					
					// Split the path by "/"
					size_t pos = 0;
					std::string token;
					while ((pos = fullPath.find('/')) != std::string::npos) 
					{
						token = fullPath.substr(0, pos);
						if (!token.empty())
						{
							pathComponents.push_back(token);
						}
						fullPath.erase(0, pos + 1);
					}
					if (!fullPath.empty())
					{
						pathComponents.push_back(fullPath);
					}
					
					// Display path components as a breadcrumb
					std::string currentPath;
					for (size_t i = 0; i < pathComponents.size(); ++i)
					{
						ImGui::SameLine();
						ImGui::Text(" > ");
						ImGui::SameLine();
						
						// Build the path up to this component
						if (currentPath.empty())
						{
							currentPath = pathComponents[i];
						}
						else
						{
							currentPath += "/" + pathComponents[i];
						}
						
						// Store path for button
						std::string buttonPath = currentPath;
						
						// If this is the last component (current directory)
						if (i == pathComponents.size() - 1)
						{
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.45f, 0.8f));
							ImGui::Button(pathComponents[i].c_str());
							ImGui::PopStyleColor();
						}
						else
						{
							if (ImGui::Button(pathComponents[i].c_str()))
							{
								// Find the entry that matches this path
								for (const auto& [name, entry] : m_assets)
								{
									if (entry.fullPath == buttonPath)
									{
										m_selectedEntry = &entry;
										m_host.SetCurrentPath(entry.fullPath);
										break;
									}
									
									// Search in children
									SearchEntryByPath(entry, buttonPath);
								}
							}
						}
					}
				}
			}
			
			ImGui::PopStyleColor(3); // Pop breadcrumb button styles
			ImGui::PopStyleVar();    // Pop frame padding
			
			ImGui::EndChild();       // End breadcrumb bar
			ImGui::PopStyleColor();  // Pop breadcrumb background color
			
			// Add a horizontal separator beneath the breadcrumb bar
			ImGui::Separator();
			
			// Now continue with the rest of the UI (two-column layout)
			ImGui::Columns(2, nullptr, true);
			static bool widthSet = false;
			if (!widthSet)
			{
				ImGui::SetColumnWidth(ImGui::GetColumnIndex(), 350.0f);
				widthSet = true;
			}

			ImGui::BeginChild("assetFolderScrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
			
			const std::string path;
			for (const auto& [name, entry]: m_assets)
			{
				RenderAssetEntry(name, entry, path);
			}
			ImGui::EndChild();
			
			ImGui::NextColumn();

			// Add a search bar at the top of the right panel
			static char searchBuffer[256] = "";
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			ImGui::InputTextWithHint("##AssetSearch", "Search assets...", searchBuffer, IM_ARRAYSIZE(searchBuffer));
			
			// Store the search string in a std::string for easier comparison
			std::string searchString = searchBuffer;

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
			ImGui::BeginChild("assetPreview", ImVec2(0, 0));
			{
				const ImVec2 size = ImGui::GetWindowSize();
				
				// Display root directory entries when no folder is selected
				if (m_selectedEntry == nullptr)
				{
					const auto colCount = static_cast<int32>(size.x / (128.0f + 10.0f + ImGui::GetStyle().ColumnsMinSpacing));
					if (colCount > 0)
					{
						ImGui::Columns(colCount, nullptr, false);
						
						const ImTextureID folderTexture = (m_folderTexture ? m_folderTexture->GetTextureObject() : nullptr);
						
						// Display all top-level entries
						for (const auto& [name, entry] : m_assets)
						{
							ImGui::PushID(entry.fullPath.c_str());
							
							if (ImGui::ImageButton(folderTexture, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1), 2, ImVec4(0, 0, 0, 0)))
							{
								m_selectedEntry = &entry;
								m_host.SetCurrentPath(entry.fullPath);
							}
							
							ImGui::TextWrapped(name.c_str());
							ImGui::PopID();
							ImGui::NextColumn();
						}
					}
				}
				else
				{
					const auto colCount = static_cast<int32>(size.x / (128.0f + 10.0f + ImGui::GetStyle().ColumnsMinSpacing));
					if (colCount > 0)
					{
						ImGui::Columns(colCount, nullptr, false);
						
						const ImTextureID folderTexture = (m_folderTexture ? m_folderTexture->GetTextureObject() : nullptr);
						
						for (const auto& [name, entry] : m_selectedEntry->children)
						{
							// Skip items that don't match the search filter
							if (!searchString.empty()) 
							{
								// Convert both strings to lowercase for case-insensitive comparison
								std::string lowerName = name;
								std::string lowerSearch = searchString;
								std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
								std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);
								
								// Skip if the name doesn't contain the search string
								if (lowerName.find(lowerSearch) == std::string::npos) 
								{
									continue;
								}
							}
							
							ImGui::PushID(entry.fullPath.c_str());

							// Is this a non-empty entry, so a folder?
							if (!entry.children.empty())
							{
								if (ImGui::ImageButton(folderTexture, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1), 2, ImVec4(0, 0, 0, 0)))
								{
									m_selectedEntry = &entry;
									m_host.SetCurrentPath(entry.fullPath);
								}
								
								ImGui::TextWrapped(name.c_str());
							}
							else
							{
								ImTextureID imTexture = nullptr;

								const auto& extension = std::filesystem::path(name).extension().string();
								auto* previewProvider = m_previewProviderManager.GetPreviewProviderForExtension(extension);
								if (previewProvider)
								{
									imTexture = previewProvider->GetAssetPreview(entry.fullPath);
								}								
								
								if (ImGui::ImageButton(imTexture, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1), 1, ImVec4(0, 0, 0, 0)))
								{
									m_host.OpenAsset(entry.fullPath);
								}

								if (ImGui::BeginPopupContextItem(entry.fullPath.c_str(), ImGuiPopupFlags_MouseButtonRight))
								{
									m_host.ShowAssetCreationContextMenu();
									m_host.ShowAssetActionContextMenu(entry.fullPath);
									ImGui::EndPopup();
								}

								ImGuiDragDropFlags src_flags = 0;
							    src_flags |= ImGuiDragDropFlags_SourceNoDisableHover;     // Keep the source displayed as hovered
							    src_flags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers; // Because our dragging is local, we disable the feature of opening foreign treenodes/tabs while dragging
							    //src_flags |= ImGuiDragDropFlags_SourceNoPreviewTooltip; // Hide the tooltip
							    if (ImGui::BeginDragDropSource(src_flags))
							    {
							        if (!(src_flags & ImGuiDragDropFlags_SourceNoPreviewTooltip))
							            ImGui::ImageButton(imTexture, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1), 1, ImVec4(0, 0, 0, 0));

							        ImGui::SetDragDropPayload(Path(entry.fullPath).extension().string().c_str(), &entry.fullPath, sizeof(entry.fullPath));
							        ImGui::EndDragDropSource();
							    }
								
								ImGui::TextWrapped(name.c_str());
							}
							
							ImGui::PopID();

							ImGui::NextColumn();
						}
					}
				}

				if (ImGui::BeginPopupContextWindow("AssetContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverExistingPopup))
			    {
					m_host.ShowAssetCreationContextMenu();
			        ImGui::EndPopup();
			    }
			}

			ImGui::EndChild();
			ImGui::PopStyleColor();
			
			ImGui::Columns(1);

		}
		ImGui::End();

		return false;
	}
}
