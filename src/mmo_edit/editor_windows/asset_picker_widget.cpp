// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "asset_picker_widget.h"

#include <algorithm>
#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
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
					result.push_back(file);
					break;
				}
			}
		}

		// Sort alphabetically for easier browsing
		std::sort(result.begin(), result.end());

		return result;
	}

	bool AssetPickerWidget::Draw(
		const char* label,
		std::string& currentAssetPath,
		const std::set<String>& extensions,
		PreviewProviderManager* previewManager,
		IAudio* audioSystem,
		float previewSize)
	{
		bool changed = false;
		
		ImGui::PushID(label);

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

			if (auto* provider = previewManager->GetPreviewProviderForExtension(extension))
			{
				if (const ImTextureID texId = provider->GetAssetPreview(currentAssetPath))
				{
					ImGui::Image(texId, ImVec2(previewSize, previewSize));
				}
			}

			ImGui::SameLine();
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

		// Get filtered asset list
		static std::vector<String> s_filteredAssets;
		static std::set<String> s_lastExtensions;
		
		// Rebuild list if extensions changed
		if (s_lastExtensions != extensions)
		{
			s_filteredAssets = GetFilteredAssets(extensions);
			s_lastExtensions = extensions;
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
			for (const auto& assetPath : s_filteredAssets)
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

		// Drag & drop support
		if (ImGui::BeginDragDropTarget())
		{
			// Accept any of the allowed extensions
			for (const auto& ext : extensions)
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ext.c_str()))
				{
					currentAssetPath = *static_cast<String*>(payload->Data);
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
