// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "asset_window.h"
#include "assets/asset_registry.h"

#include <imgui.h>

namespace mmo
{	
	AssetWindow::AssetWindow()
		: m_visible(true)
	{
	}

	void RenderAssetEntry(const std::string& name, const AssetEntry& entry, const std::string& path)
	{
		// If there are no children, we don't need to continue
		if (entry.children.empty())
		{
			ImGui::Selectable(name.c_str());
		}
		else
		{
			// There are children, render them
			if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				const auto childPath = path + "/" + name;
				for(const auto& child : entry.children)
				{
					RenderAssetEntry(child.first, child.second, childPath);
				}

				ImGui::TreePop();
			}
		}
	}

	void AddAssetToMap(AssetEntry& parent, const std::string& assetPath)
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
			// Gather a list of all assets in the registry
			const auto assets = AssetRegistry::ListFiles();
			for(const auto& asset : assets)
			{
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
