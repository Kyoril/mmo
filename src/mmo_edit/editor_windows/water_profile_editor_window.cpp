// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "water_profile_editor_window.h"
#include "editor_imgui_helpers.h"
#include "asset_picker_widget.h"

#include "terrain/constants.h"

#include <algorithm>
#include <functional>

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace mmo
{
	namespace
	{
		/// Names the liquid a profile applies to. Profile ids are terrain::WaterType values, so an
		/// id outside that enum is authored data no tile can ever use.
		const char* LiquidNameForProfileId(const uint32 id)
		{
			switch (static_cast<terrain::WaterType>(id))
			{
			case terrain::WaterType::Water:
				return "Water";
			case terrain::WaterType::Ocean:
				return "Ocean";
			case terrain::WaterType::Lava:
				return "Lava";
			case terrain::WaterType::Slime:
				return "Slime";
			default:
				return nullptr;
			}
		}

		/// Edits a packed 0xAARRGGBB colour with a colour picker, the packing water profiles use.
		void EditPackedColor(const char* label, const uint32 packed, const std::function<void(uint32)>& setter)
		{
			float rgb[3] = {
				static_cast<float>((packed >> 16) & 0xFFu) / 255.0f,
				static_cast<float>((packed >> 8) & 0xFFu) / 255.0f,
				static_cast<float>(packed & 0xFFu) / 255.0f
			};

			if (!ImGui::ColorEdit3(label, rgb))
			{
				return;
			}

			const auto toByte = [](const float value)
			{
				return static_cast<uint32>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
			};

			setter(0xFF000000u | (toByte(rgb[0]) << 16) | (toByte(rgb[1]) << 8) | toByte(rgb[2]));
		}

		/// Edits an optional asset path field. An empty path clears the field instead of storing "".
		void EditAssetField(const char* label, const String& current, const std::set<String>& extensions,
			const std::function<void(const String&)>& setter, const std::function<void()>& clearer)
		{
			String path = current;
			if (AssetPickerWidget::Draw(label, path, extensions))
			{
				if (path.empty())
				{
					clearer();
				}
				else
				{
					setter(path);
				}
			}
		}
	}

	WaterProfileEditorWindow::WaterProfileEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.waterProfiles, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Water Profiles";
	}

	void WaterProfileEditorWindow::OnNewEntry(proto::TemplateManager<proto::WaterProfiles, proto::WaterProfile>::EntryType& entry)
	{
		// Starts from a clear, stylized sea rather than from zeroes, so a new profile is visible
		// the moment the camera dips below the surface and tuning begins from something plausible.
		entry.set_name("New Water Profile");
		entry.set_fog_color(0xFF0A4257);
		entry.set_fog_density(0.03f);
		entry.set_absorption_color(0xFF661F12);
		entry.set_caustics_strength(0.5f);
		entry.set_caustics_texture("Textures/Caustics_01.htex");
		entry.set_distortion_strength(1.0f);
		entry.set_audio_lowpass_hz(900.0f);
	}

	void WaterProfileEditorWindow::DrawDetailsImpl(proto::WaterProfile& currentEntry)
	{
		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 2, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentEntry.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}

				ImGui::EndTable();
			}

			if (const char* liquidName = LiquidNameForProfileId(currentEntry.id()))
			{
				ImGui::Text("Applies to every tile painted as %s.", liquidName);
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
					"No liquid type uses id %u (1 = Water, 2 = Ocean, 3 = Lava, 4 = Slime). This profile is never applied.",
					currentEntry.id());
			}
		}

		if (const auto section = ScopedEditorSection("Surface", ImGuiTreeNodeFlags_DefaultOpen))
		{
			EditAssetField("Surface Material", currentEntry.surface_material(), asset_extensions::Materials,
				[&currentEntry](const String& path) { currentEntry.set_surface_material(path); },
				[&currentEntry]() { currentEntry.clear_surface_material(); });

			ImGui::TextDisabled("A page's own water material override still wins over this.");
		}

		if (const auto section = ScopedEditorSection("Underwater", ImGuiTreeNodeFlags_DefaultOpen))
		{
			EditPackedColor("Fog Color", currentEntry.fog_color(),
				[&currentEntry](const uint32 color) { currentEntry.set_fog_color(color); });

			float fogDensity = currentEntry.fog_density();
			if (ImGui::DragFloat("Fog Density", &fogDensity, 0.001f, 0.0f, 0.5f, "%.3f"))
			{
				currentEntry.set_fog_density(std::max(fogDensity, 0.0f));
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Per world unit. 0.03 is a clear sea with roughly 60 units of visibility;\n"
					"0.1 and above is a murky lake.");
			}

			EditPackedColor("Absorption", currentEntry.absorption_color(),
				[&currentEntry](const uint32 color) { currentEntry.set_absorption_color(color); });
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("How strongly each channel is absorbed with distance. Absorbing red most\n"
					"leaves the blue-green of deep water; absorbing blue most leaves a swampy brown.");
			}

			float distortion = currentEntry.distortion_strength();
			if (ImGui::DragFloat("Distortion", &distortion, 0.01f, 0.0f, 5.0f, "%.2f"))
			{
				currentEntry.set_distortion_strength(std::max(distortion, 0.0f));
			}

			float caustics = currentEntry.caustics_strength();
			if (ImGui::DragFloat("Caustics Strength", &caustics, 0.01f, 0.0f, 4.0f, "%.2f"))
			{
				currentEntry.set_caustics_strength(std::max(caustics, 0.0f));
			}

			EditAssetField("Caustics Texture", currentEntry.caustics_texture(), asset_extensions::Textures,
				[&currentEntry](const String& path) { currentEntry.set_caustics_texture(path); },
				[&currentEntry]() { currentEntry.clear_caustics_texture(); });
		}

		if (const auto section = ScopedEditorSection("Audio", ImGuiTreeNodeFlags_DefaultOpen))
		{
			float lowPass = currentEntry.audio_lowpass_hz();
			if (ImGui::DragFloat("Low-Pass Cutoff", &lowPass, 10.0f, 0.0f, 22000.0f, "%.0f Hz"))
			{
				currentEntry.set_audio_lowpass_hz(std::clamp(lowPass, 0.0f, 22000.0f));
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Muffles all audio while the player is submerged. 0 leaves audio untouched.");
			}
		}
	}
}
