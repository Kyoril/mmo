// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "asset_window.h"

#include <imgui.h>
#include <fstream>
#include <algorithm>

#include "assets/asset_registry.h"
#include "graphics/texture.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"
#include "nlohmann/json.hpp"

namespace mmo
{
	static constexpr const char* FavoritesFileName = "editor_favorites.json";

	void AssetWindow::RebuildAssetList()
	{
		// Store the current path before rebuilding
		std::string currentPath = m_selectedEntry ? m_selectedEntry->fullPath : "";
		m_assets.clear();
		m_selectedEntry = nullptr;

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

		// Restore the previous selection if possible
		if (!currentPath.empty())
		{
			bool found = false;
			for (auto& [name, entry] : m_assets)
			{
				if (entry.fullPath == currentPath)
				{
					m_selectedEntry = &entry;
					m_host.SetCurrentPath(entry.fullPath);
					found = true;
					break;
				}

				if (SearchEntryByPath(entry, currentPath))
				{
					found = true;
					break;
				}
			}

			if (!found && !currentPath.empty())
			{
				std::string parentPath = currentPath;
				while (!parentPath.empty() && !found)
				{
					size_t lastSlash = parentPath.find_last_of('/');
					if (lastSlash != std::string::npos)
					{
						parentPath = parentPath.substr(0, lastSlash);

						for (auto& [name, entry] : m_assets)
						{
							if (entry.fullPath == parentPath)
							{
								m_selectedEntry = &entry;
								m_host.SetCurrentPath(entry.fullPath);
								found = true;
								break;
							}

							if (SearchEntryByPath(entry, parentPath))
							{
								found = true;
								break;
							}
						}
					}
					else
					{
						break;
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
			std::string currentPath = m_selectedEntry ? m_selectedEntry->fullPath : "";

			m_assets.clear();

			const AssetEntry* oldSelection = m_selectedEntry;
			m_selectedEntry = nullptr;
			RebuildAssetList();

			if (!currentPath.empty())
			{
				bool found = false;
				for (const auto& [name, entry] : m_assets)
				{
					if (entry.fullPath == currentPath)
					{
						m_selectedEntry = &entry;
						m_host.SetCurrentPath(entry.fullPath);
						found = true;
						break;
					}

					if (SearchEntryByPath(entry, currentPath))
					{
						found = true;
						break;
					}
				}

				if (!found && !currentPath.empty())
				{
					std::string parentPath = currentPath;
					while (!parentPath.empty() && !found)
					{
						size_t lastSlash = parentPath.find_last_of('/');
						if (lastSlash != std::string::npos)
						{
							parentPath = parentPath.substr(0, lastSlash);

							for (const auto& [name, entry] : m_assets)
							{
								if (entry.fullPath == parentPath)
								{
									m_selectedEntry = &entry;
									m_host.SetCurrentPath(entry.fullPath);
									found = true;
									break;
								}

								if (SearchEntryByPath(entry, parentPath))
								{
									found = true;
									break;
								}
							}
						}
						else
						{
							break;
						}
					}
				}
			}
		});

		m_folderTexture = TextureManager::Get().CreateOrRetrieve("Editor/Folder_BaseHi_256x.htex");
		RebuildAssetList();
		LoadFavorites();
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

		if (ImGui::TreeNodeEx(name.c_str(), flags))
		{
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				m_selectedEntry = &entry;
				m_showingFavorites = false;
				m_host.SetCurrentPath(entry.fullPath);
			}

			if (ImGui::BeginPopupContextItem(entry.fullPath.c_str(), ImGuiPopupFlags_MouseButtonRight))
			{
				if (ImGui::MenuItem("Delete Folder"))
				{
					m_folderPendingDelete = entry.fullPath;
					m_showFolderDeleteConfirmation = true;
				}
				ImGui::EndPopup();
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
		const auto separatorIndex = assetPath.find('/');
		if (separatorIndex == std::string::npos)
		{
			parent.children[assetPath] = { parent.fullPath + "/" + assetPath, {}};
			return;
		}

		const auto childName = assetPath.substr(0, separatorIndex);
		const auto remainingPath = assetPath.substr(separatorIndex + 1);

		const auto childIt = parent.children.find(childName);
		if (childIt == parent.children.end())
		{
			AssetEntry& child = (parent.children[childName] = { parent.fullPath + "/" + childName, {} });
			AddAssetToMap(child, remainingPath);
		}
		else
		{
			AddAssetToMap(childIt->second, remainingPath);
		}
	}

	bool AssetWindow::IsFavorite(const std::string& path) const
	{
		return std::find(m_favorites.begin(), m_favorites.end(), path) != m_favorites.end();
	}

	void AssetWindow::AddToFavorites(const std::string& path)
	{
		if (!IsFavorite(path))
		{
			m_favorites.push_back(path);
			SaveFavorites();
		}
	}

	void AssetWindow::RemoveFromFavorites(const std::string& path)
	{
		auto it = std::find(m_favorites.begin(), m_favorites.end(), path);
		if (it != m_favorites.end())
		{
			m_favorites.erase(it);
			SaveFavorites();
		}
	}

	void AssetWindow::SaveFavorites() const
	{
		nlohmann::json doc;
		doc["favorites"] = m_favorites;

		std::ofstream file(FavoritesFileName);
		if (file)
		{
			file << doc.dump(2);
		}
		else
		{
			WLOG("Could not save favorites to " << FavoritesFileName);
		}
	}

	void AssetWindow::LoadFavorites()
	{
		std::ifstream file(FavoritesFileName);
		if (!file)
		{
			return;
		}

		try
		{
			nlohmann::json doc;
			file >> doc;

			if (doc.contains("favorites") && doc["favorites"].is_array())
			{
				m_favorites = doc["favorites"].get<std::vector<std::string>>();
			}
		}
		catch (const nlohmann::json::exception& e)
		{
			WLOG("Failed to parse favorites file: " << e.what());
		}
	}

	void AssetWindow::RenderAssetGrid(const std::vector<std::string>& paths, const std::string& searchString, bool isFavoritesView)
	{
		const ImVec2 size = ImGui::GetWindowSize();
		const auto colCount = static_cast<int32>(size.x / (128.0f + 10.0f + ImGui::GetStyle().ColumnsMinSpacing));
		if (colCount <= 0)
		{
			return;
		}

		ImGui::Columns(colCount, nullptr, false);

		for (const auto& fullPath : paths)
		{
			// Extract filename for label and extension for preview
			const std::string fileName = std::filesystem::path(fullPath).filename().string();
			const std::string extension = std::filesystem::path(fullPath).extension().string();

			// Apply search filter
			if (!searchString.empty())
			{
				std::string lowerName = fileName;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
				if (lowerName.find(searchString) == std::string::npos)
				{
					continue;
				}
			}

			ImGui::PushID(fullPath.c_str());

			// Resolve preview texture
			ImTextureID imTexture = nullptr;
			auto* previewProvider = m_previewProviderManager.GetPreviewProviderForExtension(extension);
			if (previewProvider)
			{
				imTexture = previewProvider->GetAssetPreview(fullPath);
			}

			if (ImGui::ImageButton(imTexture, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1), 1, ImVec4(0, 0, 0, 0)))
			{
				m_host.OpenAsset(fullPath);
			}

			// Context menu
			if (ImGui::BeginPopupContextItem(fullPath.c_str(), ImGuiPopupFlags_MouseButtonRight))
			{
				if (isFavoritesView)
				{
					if (ImGui::MenuItem("Remove from Favorites"))
					{
						RemoveFromFavorites(fullPath);
					}
					ImGui::Separator();
				}
				else
				{
					if (ImGui::MenuItem("Add to Favorites"))
					{
						AddToFavorites(fullPath);
					}
					ImGui::Separator();
				}

				m_host.ShowAssetCreationContextMenu();
				m_host.ShowAssetActionContextMenu(fullPath);

				ImGui::Separator();
				if (ImGui::MenuItem("Delete"))
				{
					m_assetPendingDelete = fullPath;
					m_showDeleteConfirmation = true;
				}

				ImGui::EndPopup();
			}

			// Drag & drop source
			ImGuiDragDropFlags srcFlags = 0;
			srcFlags |= ImGuiDragDropFlags_SourceNoDisableHover;
			srcFlags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers;
			if (ImGui::BeginDragDropSource(srcFlags))
			{
				if (!(srcFlags & ImGuiDragDropFlags_SourceNoPreviewTooltip))
				{
					ImGui::ImageButton(imTexture, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1), 1, ImVec4(0, 0, 0, 0));
				}

				ImGui::SetDragDropPayload(std::filesystem::path(fullPath).extension().string().c_str(), &fullPath, sizeof(fullPath));
				ImGui::EndDragDropSource();
			}

			ImGui::TextWrapped(fileName.c_str());

			ImGui::PopID();
			ImGui::NextColumn();
		}
	}

	bool AssetWindow::Draw()
	{
		if (ImGui::Begin(m_name.c_str(), &m_visible))
		{
			// -----------------------------------------------------------------------
			// Breadcrumb bar
			// -----------------------------------------------------------------------
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.12f, 1.0f));
			ImGui::BeginChild("BreadcrumbBar", ImVec2(ImGui::GetContentRegionAvail().x, 24), false,
			                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

			ImGui::SetCursorPosY((20 - ImGui::GetTextLineHeightWithSpacing()) * 0.5f);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.25f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.45f, 1.0f));

			if (m_showingFavorites)
			{
				// Root is clickable to go back to normal view
				if (ImGui::Button("Root"))
				{
					m_selectedEntry = nullptr;
					m_showingFavorites = false;
					m_host.SetCurrentPath("");
				}
				ImGui::SameLine();
				ImGui::Text(" > ");
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.45f, 0.8f));
				ImGui::Button("Favorites");
				ImGui::PopStyleColor();
			}
			else if (m_selectedEntry == nullptr)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.45f, 0.8f));
				ImGui::Button("Root");
				ImGui::PopStyleColor();
			}
			else
			{
				if (ImGui::Button("Root"))
				{
					m_selectedEntry = nullptr;
					m_showingFavorites = false;
					m_host.SetCurrentPath("");
				}
				else if (!m_selectedEntry->fullPath.empty())
				{
					std::string fullPath = m_selectedEntry->fullPath;
					std::vector<std::string> pathComponents;

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

					std::string currentPath;
					for (size_t i = 0; i < pathComponents.size(); ++i)
					{
						ImGui::SameLine();
						ImGui::Text(" > ");
						ImGui::SameLine();

						if (currentPath.empty())
						{
							currentPath = pathComponents[i];
						}
						else
						{
							currentPath += "/" + pathComponents[i];
						}

						std::string buttonPath = currentPath;

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
								for (const auto& [name, entry] : m_assets)
								{
									if (entry.fullPath == buttonPath)
									{
										m_selectedEntry = &entry;
										m_host.SetCurrentPath(entry.fullPath);
										break;
									}

									SearchEntryByPath(entry, buttonPath);
								}
							}
						}
					}
				}
			}

			ImGui::PopStyleColor(3);
			ImGui::PopStyleVar();

			ImGui::EndChild();
			ImGui::PopStyleColor();

			ImGui::Separator();

			// -----------------------------------------------------------------------
			// Two-column layout: left = folder tree, right = asset grid
			// -----------------------------------------------------------------------
			ImGui::Columns(2, nullptr, true);

			static bool firstFrame = true;
			static float lastWindowWidth = ImGui::GetWindowWidth();
			float currentWindowWidth = ImGui::GetWindowWidth();

			if (firstFrame || std::abs(currentWindowWidth - lastWindowWidth) > 1.0f)
			{
				ImGui::SetColumnWidth(0, m_columnWidth);
				lastWindowWidth = currentWindowWidth;
				firstFrame = false;
			}
			else
			{
				float currentColumnWidth = ImGui::GetColumnWidth(0);
				if (std::abs(currentColumnWidth - m_columnWidth) > 1.0f)
				{
					m_columnWidth = currentColumnWidth;
				}
			}

			// -----------------------------------------------------------------------
			// Left panel: Favorites shortcut + folder tree
			// -----------------------------------------------------------------------

			// Favorites entry at the top of the tree
			{
				ImGuiTreeNodeFlags favFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
				if (m_showingFavorites)
				{
					favFlags |= ImGuiTreeNodeFlags_Selected;
				}

				ImGui::TreeNodeEx("★ Favorites", favFlags);
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					m_showingFavorites = true;
					m_selectedEntry = nullptr;
					m_host.SetCurrentPath("");
				}
			}

			ImGui::Separator();

			// Folder search bar
			static char folderSearchBuffer[256] = "";
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			ImGui::InputTextWithHint("##FolderSearch", "Filter folders...", folderSearchBuffer, IM_ARRAYSIZE(folderSearchBuffer));
			std::string folderSearchString = folderSearchBuffer;
			std::transform(folderSearchString.begin(), folderSearchString.end(), folderSearchString.begin(), ::tolower);

			ImGui::BeginChild("assetFolderScrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

			const std::string path;
			for (const auto& [name, entry]: m_assets)
			{
				if (!folderSearchString.empty())
				{
					std::string lowerName = name;
					std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

					if (lowerName.find(folderSearchString) == std::string::npos &&
					    !FolderContainsSearchString(entry, folderSearchString))
					{
						continue;
					}
				}

				RenderAssetEntry(name, entry, path);
			}
			ImGui::EndChild();

			ImGui::NextColumn();

			// -----------------------------------------------------------------------
			// Right panel: asset search bar + thumbnail grid
			// -----------------------------------------------------------------------
			static char searchBuffer[256] = "";
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			ImGui::InputTextWithHint("##AssetSearch", "Search assets...", searchBuffer, IM_ARRAYSIZE(searchBuffer));

			std::string searchString = searchBuffer;

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			ImGui::BeginChild("assetPreview", ImVec2(0, 0));
			{
				if (m_showingFavorites)
				{
					// ------------------------------------------------------------------
					// Favorites view: flat list of favorited assets
					// ------------------------------------------------------------------
					if (m_favorites.empty())
					{
						ImGui::TextDisabled("No favorites yet.");
						ImGui::TextDisabled("Right-click any asset and choose \"Add to Favorites\".");
					}
					else
					{
						RenderAssetGrid(m_favorites, searchString, true);
					}
				}
				else
				{
					// ------------------------------------------------------------------
					// Normal folder view
					// ------------------------------------------------------------------
					const ImVec2 size = ImGui::GetWindowSize();
					const auto colCount = static_cast<int32>(size.x / (128.0f + 10.0f + ImGui::GetStyle().ColumnsMinSpacing));
					if (colCount > 0)
					{
						ImGui::Columns(colCount, nullptr, false);

						const ImTextureID folderTexture = (m_folderTexture ? m_folderTexture->GetTextureObject() : nullptr);

						if (m_selectedEntry == nullptr)
						{
							// Root view: show top-level folder icons
							for (const auto& [name, entry] : m_assets)
							{
								ImGui::PushID(entry.fullPath.c_str());

								if (ImGui::ImageButton(folderTexture, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1), 2, ImVec4(0, 0, 0, 0)))
								{
									m_selectedEntry = &entry;
									m_showingFavorites = false;
									m_host.SetCurrentPath(entry.fullPath);
								}

								if (ImGui::BeginPopupContextItem(entry.fullPath.c_str(), ImGuiPopupFlags_MouseButtonRight))
								{
									if (ImGui::MenuItem("Delete Folder"))
									{
										m_folderPendingDelete = entry.fullPath;
										m_showFolderDeleteConfirmation = true;
									}
									ImGui::EndPopup();
								}

								ImGui::TextWrapped(name.c_str());
								ImGui::PopID();
								ImGui::NextColumn();
							}
						}
						else
						{
							// Folder view: show children (sub-folders and files)
							for (const auto& [name, entry] : m_selectedEntry->children)
							{
								// Apply search filter
								if (!searchString.empty())
								{
									std::string lowerName = name;
									std::string lowerSearch = searchString;
									std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
									std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);

									if (lowerName.find(lowerSearch) == std::string::npos)
									{
										continue;
									}
								}

								ImGui::PushID(entry.fullPath.c_str());

								if (!entry.children.empty())
								{
									// Sub-folder
									if (ImGui::ImageButton(folderTexture, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1), 2, ImVec4(0, 0, 0, 0)))
									{
										m_selectedEntry = &entry;
										m_showingFavorites = false;
										m_host.SetCurrentPath(entry.fullPath);
									}

									if (ImGui::BeginPopupContextItem(entry.fullPath.c_str(), ImGuiPopupFlags_MouseButtonRight))
									{
										if (ImGui::MenuItem("Delete Folder"))
										{
											m_folderPendingDelete = entry.fullPath;
											m_showFolderDeleteConfirmation = true;
										}
										ImGui::EndPopup();
									}

									ImGui::TextWrapped(name.c_str());
								}
								else
								{
									// File asset
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
										if (IsFavorite(entry.fullPath))
										{
											if (ImGui::MenuItem("Remove from Favorites"))
											{
												RemoveFromFavorites(entry.fullPath);
											}
										}
										else
										{
											if (ImGui::MenuItem("Add to Favorites"))
											{
												AddToFavorites(entry.fullPath);
											}
										}
										ImGui::Separator();

										m_host.ShowAssetCreationContextMenu();
										m_host.ShowAssetActionContextMenu(entry.fullPath);

										ImGui::Separator();
										if (ImGui::MenuItem("Delete"))
										{
											m_assetPendingDelete = entry.fullPath;
											m_showDeleteConfirmation = true;
										}

										ImGui::EndPopup();
									}

									// Drag & drop source
									ImGuiDragDropFlags srcFlags = 0;
									srcFlags |= ImGuiDragDropFlags_SourceNoDisableHover;
									srcFlags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers;
									if (ImGui::BeginDragDropSource(srcFlags))
									{
										if (!(srcFlags & ImGuiDragDropFlags_SourceNoPreviewTooltip))
										{
											ImGui::ImageButton(imTexture, ImVec2(128, 128), ImVec2(0, 0), ImVec2(1, 1), 1, ImVec4(0, 0, 0, 0));
										}

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
			}

			ImGui::EndChild();
			ImGui::PopStyleColor();

			ImGui::Columns(1);
		}

		// -----------------------------------------------------------------------
		// Delete asset confirmation dialog
		// -----------------------------------------------------------------------
		if (m_showDeleteConfirmation)
		{
			ImGui::OpenPopup("Delete Asset##Confirmation");
		}

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Appearing);

		if (ImGui::BeginPopupModal("Delete Asset##Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Are you sure you want to delete:");
			ImGui::TextWrapped(m_assetPendingDelete.c_str());
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;

			if (ImGui::Button("Delete", ImVec2(buttonWidth, 0)))
			{
				if (AssetRegistry::RemoveFile(m_assetPendingDelete))
				{
					ILOG("Asset deleted: " << m_assetPendingDelete);
					// Remove from favorites too if it was favorited
					RemoveFromFavorites(m_assetPendingDelete);
					RebuildAssetList();
				}
				else
				{
					WLOG("Failed to delete asset: " << m_assetPendingDelete);
				}

				m_showDeleteConfirmation = false;
				m_assetPendingDelete.clear();
				ImGui::CloseCurrentPopup();
			}

			ImGui::SetItemDefaultFocus();
			ImGui::SameLine();

			if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
			{
				m_showDeleteConfirmation = false;
				m_assetPendingDelete.clear();
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
		else
		{
			m_showDeleteConfirmation = false;
		}

		// -----------------------------------------------------------------------
		// Delete folder confirmation dialog
		// -----------------------------------------------------------------------
		if (m_showFolderDeleteConfirmation)
		{
			ImGui::OpenPopup("Delete Folder##Confirmation");
		}

		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(450, 180), ImGuiCond_Appearing);

		if (ImGui::BeginPopupModal("Delete Folder##Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Are you sure you want to delete this folder and all its contents:");
			ImGui::TextWrapped(m_folderPendingDelete.c_str());
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;

			if (ImGui::Button("Delete", ImVec2(buttonWidth, 0)))
			{
				const auto allFiles = AssetRegistry::ListFiles();
				m_filesToDelete.clear();
				m_filesDeletedCount = 0;

				for (const auto& file : allFiles)
				{
					if (file.starts_with(m_folderPendingDelete + "/") || file == m_folderPendingDelete)
					{
						m_filesToDelete.push_back(file);
					}
				}

				m_showFolderDeleteProgress = true;
				m_showFolderDeleteConfirmation = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SetItemDefaultFocus();
			ImGui::SameLine();

			if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
			{
				m_showFolderDeleteConfirmation = false;
				m_folderPendingDelete.clear();
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
		else
		{
			m_showFolderDeleteConfirmation = false;
		}

		// -----------------------------------------------------------------------
		// Delete folder progress dialog
		// -----------------------------------------------------------------------
		if (m_showFolderDeleteProgress)
		{
			ImGui::OpenPopup("Deleting Folder##Progress");
		}

		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(450, 150), ImGuiCond_Appearing);

		if (ImGui::BeginPopupModal("Deleting Folder##Progress", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
		{
			if (!m_filesToDelete.empty() && m_filesDeletedCount < m_filesToDelete.size())
			{
				if (AssetRegistry::RemoveFile(m_filesToDelete[m_filesDeletedCount]))
				{
					RemoveFromFavorites(m_filesToDelete[m_filesDeletedCount]);
					m_filesDeletedCount++;
				}
				else
				{
					m_filesDeletedCount++;
				}
			}

			float progress = m_filesToDelete.empty() ? 1.0f : static_cast<float>(m_filesDeletedCount) / static_cast<float>(m_filesToDelete.size());
			ImGui::Text("Deleting folder contents...");
			ImGui::ProgressBar(progress, ImVec2(-1, 0));
			ImGui::Text("%zu / %zu files deleted", m_filesDeletedCount, m_filesToDelete.size());

			if (m_filesDeletedCount >= m_filesToDelete.size())
			{
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				if (ImGui::Button("Close", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
				{
					const std::string currentPath = m_selectedEntry ? m_selectedEntry->fullPath : "";
					if (!currentPath.empty() && (currentPath == m_folderPendingDelete ||
						currentPath.find(m_folderPendingDelete + "/") == 0))
					{
						m_selectedEntry = nullptr;
						m_host.SetCurrentPath("");
					}

					ILOG("Folder deleted: " << m_folderPendingDelete << " (" << m_filesDeletedCount << " files)");
					RebuildAssetList();

					m_showFolderDeleteProgress = false;
					m_folderPendingDelete.clear();
					m_filesToDelete.clear();
					m_filesDeletedCount = 0;
					ImGui::CloseCurrentPopup();
				}
			}

			ImGui::EndPopup();
		}
		else if (!m_showFolderDeleteProgress)
		{
			m_filesToDelete.clear();
			m_filesDeletedCount = 0;
		}

		ImGui::End();

		return false;
	}
}
