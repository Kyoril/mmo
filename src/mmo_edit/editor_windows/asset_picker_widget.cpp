// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "asset_picker_widget.h"

#include <algorithm>
#include <map>
#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "locale_asset_utils.h"
#include "preview_providers/preview_provider_manager.h"
#include "shared/audio/audio.h"

namespace mmo
{
	std::vector<String> AssetPickerWidget::GetFilteredAssets(const std::set<String>& extensions)
	{
		std::vector<String> result;
		const std::vector<std::string> allFiles = AssetRegistry::ListFiles();

		for (const auto& file : allFiles)
		{
			// Check if file has any of the allowed extensions
			for (const auto& ext : extensions)
			{
				if (file.ends_with(ext))
				{
					// Locale files appear twice in the editor's registry: once under their on-disk
					// "Locales/Locale_<code>/" path and once under their locale-relative path (via the
					// mounted locale archive). Only the latter resolves in hpak release builds, so
					// offer only that form.
					String path = file;
					NormalizeLocaleAssetPath(path);
					result.push_back(std::move(path));
					break;
				}
			}
		}

		// Sort alphabetically for easier browsing and remove locale duplicates
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());

		return result;
	}

	bool AssetPickerWidget::Draw(
		const char* label,
		std::string& currentAssetPath,
		const std::set<String>& extensions,
		PreviewProviderManager* previewManager,
		IAudio* audioSystem,
		float previewSize,
		std::function<void(const std::string&)> onNavigateCallback)
	{
		bool changed = false;

		ImGui::PushID(label);

		// Older data may still reference locale assets by their on-disk "Locales/Locale_<code>/"
		// path, which doesn't resolve in hpak release builds (the client mounts the locale archive
		// as an asset root). Migrate such paths to their locale-relative form on the fly.
		if (NormalizeLocaleAssetPath(currentAssetPath))
		{
			changed = true;
		}

		// Check if we're dealing with audio files
		const bool isAudio = extensions.count(".wav") || extensions.count(".ogg") || extensions.count(".mp3");

		// Preview image (if available)
		if (previewManager && !currentAssetPath.empty())
		{
			// Get extension for preview provider lookup
			String extension;
			const size_t dotPos = currentAssetPath.find_last_of('.');
			if (dotPos != std::string::npos)
			{
				extension = currentAssetPath.substr(dotPos);
			}

			bool hasPreview = false;
			if (auto* provider = previewManager->GetPreviewProviderForExtension(extension))
			{
				if (const ImTextureID texId = provider->GetAssetPreview(currentAssetPath))
				{
					ImGui::Image(texId, ImVec2(previewSize, previewSize));
					hasPreview = true;
				}
			}

			if (hasPreview)
			{
				ImGui::SameLine();
			}
		}

		// Audio preview button (if audio system available and audio file selected)
		if (audioSystem && isAudio && !currentAssetPath.empty())
		{
			if (ImGui::Button("Preview"))
			{
				// Create and play sound
				SoundIndex soundIdx = audioSystem->FindSound(currentAssetPath, SoundType::Sound2D);
				if (soundIdx == InvalidSound)
				{
					soundIdx = audioSystem->CreateSound(currentAssetPath);
				}
				if (soundIdx != InvalidSound)
				{
					ChannelIndex channel = InvalidChannel;
					audioSystem->PlaySound(soundIdx, &channel);
				}
			}

			ImGui::SameLine();
		}

		// Get filtered asset list, cached per extension set so multiple pickers with different
		// extension sets can draw in the same frame without rebuilding each other's lists
		static std::map<std::set<String>, std::vector<String>> s_filteredAssetCache;

		auto cacheIt = s_filteredAssetCache.find(extensions);
		if (cacheIt == s_filteredAssetCache.end())
		{
			cacheIt = s_filteredAssetCache.emplace(extensions, GetFilteredAssets(extensions)).first;
		}

		const std::vector<String>& filteredAssets = cacheIt->second;

		// When a navigate callback is provided, reserve space for a small button next to the combo
		const float navButtonWidth = onNavigateCallback
			? (ImGui::CalcTextSize(">>").x + ImGui::GetStyle().FramePadding.x * 2.0f)
			: 0.0f;
		if (onNavigateCallback)
		{
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - navButtonWidth - ImGui::GetStyle().ItemSpacing.x);
		}

		// Combo box for asset selection
		if (ImGui::BeginCombo(label, currentAssetPath.empty() ? "None" : currentAssetPath.c_str(), ImGuiComboFlags_HeightLarge))
		{
			// Search filter
			static char searchBuffer[256] = "";
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextWithHint("##Search", "Search...", searchBuffer, IM_ARRAYSIZE(searchBuffer));

			const std::string searchText = searchBuffer;
			std::string lowerSearch = searchText;
			std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);

			// "None" option
			if (ImGui::Selectable("None", currentAssetPath.empty()))
			{
				currentAssetPath.clear();
				changed = true;
			}

			ImGui::Separator();

			// Draw filtered list
			ImGui::BeginChild("AssetList", ImVec2(0, 300), false);
			for (const auto& assetPath : filteredAssets)
			{
				// Apply search filter
				if (!lowerSearch.empty())
				{
					std::string lowerAsset = assetPath;
					std::transform(lowerAsset.begin(), lowerAsset.end(), lowerAsset.begin(), ::tolower);

					if (lowerAsset.find(lowerSearch) == std::string::npos)
					{
						continue;
					}
				}

				const bool isSelected = (currentAssetPath == assetPath);
				if (ImGui::Selectable(assetPath.c_str(), isSelected))
				{
					currentAssetPath = assetPath;
					changed = true;
				}

				if (isSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndChild();

			ImGui::EndCombo();
		}

		// Navigate button — always rendered when a callback is provided so the layout is stable
		if (onNavigateCallback)
		{
			ImGui::SameLine();
			ImGui::BeginDisabled(currentAssetPath.empty());
			if (ImGui::Button(">>", ImVec2(navButtonWidth, 0)))
			{
				onNavigateCallback(currentAssetPath);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Navigate to this asset in the Asset Browser");
			}
		}

		// Drag & drop support
		if (ImGui::BeginDragDropTarget())
		{
			// Accept any of the allowed extensions
			for (const auto& ext : extensions)
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ext.c_str()))
				{
					currentAssetPath = *static_cast<String*>(payload->Data);
					NormalizeLocaleAssetPath(currentAssetPath);
					changed = true;
					break;
				}
			}

			ImGui::EndDragDropTarget();
		}

		ImGui::PopID();

		return changed;
	}
}
