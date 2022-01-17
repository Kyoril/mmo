// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "asset_window.h"
#include "assets/asset_registry.h"

#include "log/default_log_levels.h"

#include <imgui.h>

#include "graphics/texture_mgr.h"
#include "graphics/texture.h"

namespace mmo
{	
	AssetWindow::AssetWindow()
		: m_visible(true)
	{
	}

	void AssetWindow::RenderAssetEntry(const std::string& name, const AssetEntry& entry, const std::string& path)
	{
		// If there are no children, we don't need to continue
		if (entry.children.empty())
		{
			return;
		}
		
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
		if (m_selectedEntry == &entry)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}

		// There are children, render them
		if (ImGui::TreeNodeEx(name.c_str(), flags))
		{
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				DLOG("Selected: " << path << "/" << name);
				m_selectedPath = path + "/" + name;
				m_selectedEntry = &entry;
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
		// Anything to draw at all?
		if (!m_visible)
			return false;

		// Gather a list of all assets
		if (m_assets.empty())
		{
			static bool s_folderIconLoaded = false;
			if (!s_folderIconLoaded)
			{
				m_folderTexture = TextureManager::Get().CreateOrRetrieve("Editor/Folder_BaseHi_256x.htex");
				s_folderIconLoaded = true;
			}

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
					}
					else
					{
						AddAssetToMap(entryIt->second, remainingPath);
					}
				}
			}
		}
		
		// Add the viewport
		if (ImGui::Begin("Assets", &m_visible))
		{
			ImGui::Columns(2, nullptr, true);
			ImGui::BeginChild("assetFolderScrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
			
			const std::string path;
			for (const auto& asset: m_assets)
			{
				RenderAssetEntry(asset.first, asset.second, path);
			}
			

			ImGui::EndChild();
			
			ImGui::NextColumn();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
			ImGui::BeginChild("assetPreview", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
			{
				const ImVec2 size = ImGui::GetWindowSize();

				
				const int32 colCount = static_cast<int32>(size.x / (128.0f + 10.0f + ImGui::GetStyle().ColumnsMinSpacing));
				if (colCount > 0)
				{
					ImGui::Columns(colCount, nullptr, false);

					if (m_selectedEntry)
					{
						const ImTextureID folderTexture = m_folderTexture ? m_folderTexture->GetTextureObject() : nullptr;
						
						for (const auto& [name, entry] : m_selectedEntry->children)
						{
							if (!entry.children.empty())
							{
								ImGui::Spacing();
								ImGui::ImageButton(folderTexture, ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0), 1, ImVec4(0, 0, 0, 0));
								ImGui::TextWrapped(name.c_str());
							}
							else
							{
								ImTextureID imTexture = nullptr;
								if (name.ends_with(".htex"))
								{
									const auto texture = TextureManager::Get().CreateOrRetrieve(entry.fullPath);
									if (texture)
									{
										imTexture = texture->GetTextureObject();
									}
								}

								ImGui::Spacing();
								ImGui::ImageButton(imTexture, ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0), 1, ImVec4(0, 0, 0, 0));
								ImGui::TextWrapped(name.c_str());
							}
							
							ImGui::NextColumn();
						}
					}
				}
				
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			
			ImGui::Columns(1);
		}
		ImGui::End();

		return false;
	}

	bool AssetWindow::DrawViewMenuItem()
	{
		return false;
	}
}
