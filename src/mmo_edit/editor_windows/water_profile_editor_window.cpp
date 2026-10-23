// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "water_profile_editor_window.h"
#include "editor_imgui_helpers.h"
#include "asset_picker_widget.h"
#include "sound_entry_combo.h"

#include "terrain/constants.h"
#include "deferred_shading/ocean_profile.h"

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

		if (const auto section = ScopedEditorSection("Ocean Waves", currentEntry.id() == static_cast<uint32>(terrain::WaterType::Ocean) ? ImGuiTreeNodeFlags_DefaultOpen : 0))
		{
			// Unset fields show (and keep) the engine default; the first edit stores a value.
			const OceanSettings defaults;
			const auto oceanFloat = [](const char* label, const bool has, const float value, const float fallback, const float speed,
				const float minimum, const float maximum, const char* format, const char* tooltip, const std::function<void(float)>& setter)
			{
				float current = has ? value : fallback;
				if (ImGui::DragFloat(label, &current, speed, minimum, maximum, format))
				{
					setter(std::clamp(current, minimum, maximum));
				}
				if (tooltip && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", tooltip);
				}
			};

			if (currentEntry.id() != static_cast<uint32>(terrain::WaterType::Ocean))
			{
				ImGui::TextDisabled("Only the Ocean liquid (id 2) is drawn by the FFT ocean.");
			}

			ImGui::TextDisabled("Waves");
			oceanFloat("Wind Speed", currentEntry.has_ocean_wind_speed(), currentEntry.ocean_wind_speed(), defaults.windSpeed, 0.05f, 0.5f, 40.0f, "%.1f m/s",
				"Main driver of wave height and wavelength. 5 is a light breeze, 9 a lively sea, 15 rough.",
				[&currentEntry](const float v) { currentEntry.set_ocean_wind_speed(v); });
			oceanFloat("Wind Direction", currentEntry.has_ocean_wind_direction(), currentEntry.ocean_wind_direction(), defaults.windDirection, 0.5f, -360.0f, 360.0f, "%.0f deg",
				"Direction the waves travel toward. 0 = +X, 90 = +Z.",
				[&currentEntry](const float v) { currentEntry.set_ocean_wind_direction(v); });
			oceanFloat("Fetch", currentEntry.has_ocean_fetch(), currentEntry.ocean_fetch(), defaults.fetch, 1.0f, 0.1f, 2000.0f, "%.0f km",
				"How far the wind has blown over open water. Short = young, choppy sea; long = rolling swell.",
				[&currentEntry](const float v) { currentEntry.set_ocean_fetch(v); });
			oceanFloat("Spread", currentEntry.has_ocean_spread(), currentEntry.ocean_spread(), defaults.spread, 0.05f, 0.0f, 64.0f, "%.1f",
				"Low = waves from every direction, high = long parallel crests.",
				[&currentEntry](const float v) { currentEntry.set_ocean_spread(v); });
			oceanFloat("Wave Scale", currentEntry.has_ocean_wave_scale(), currentEntry.ocean_wave_scale(), defaults.waveScale, 0.01f, 0.0f, 4.0f, "%.2f",
				"Multiplier on wave height.",
				[&currentEntry](const float v) { currentEntry.set_ocean_wave_scale(v); });
			oceanFloat("Choppiness", currentEntry.has_ocean_choppiness(), currentEntry.ocean_choppiness(), defaults.choppiness, 0.01f, 0.0f, 3.0f, "%.2f",
				"Sharpens crests and widens troughs. Too high makes crests fold over.",
				[&currentEntry](const float v) { currentEntry.set_ocean_choppiness(v); });
			oceanFloat("Wave Speed", currentEntry.has_ocean_wave_speed(), currentEntry.ocean_wave_speed(), defaults.timeScale, 0.01f, 0.0f, 4.0f, "%.2f",
				"Multiplier on how fast the waves move.",
				[&currentEntry](const float v) { currentEntry.set_ocean_wave_speed(v); });

			ImGui::Separator();
			ImGui::TextDisabled("Colour");
			EditPackedColor("Trough Color", currentEntry.has_ocean_trough_color() ? currentEntry.ocean_trough_color() : PackOceanProfileColor(defaults.troughColor),
				[&currentEntry](const uint32 color) { currentEntry.set_ocean_trough_color(color); });
			EditPackedColor("Crest Color", currentEntry.has_ocean_crest_color() ? currentEntry.ocean_crest_color() : PackOceanProfileColor(defaults.crestColor),
				[&currentEntry](const uint32 color) { currentEntry.set_ocean_crest_color(color); });
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Colour of the light glowing through thin wave crests when looking toward the sun.");
			}
			oceanFloat("Absorption R", currentEntry.has_ocean_absorption_r(), currentEntry.ocean_absorption_r(), defaults.absorption.x, 0.005f, 0.0f, 4.0f, "%.3f /m",
				"Light absorbed per metre. Lower values make clearer water.",
				[&currentEntry](const float v) { currentEntry.set_ocean_absorption_r(v); });
			oceanFloat("Absorption G", currentEntry.has_ocean_absorption_g(), currentEntry.ocean_absorption_g(), defaults.absorption.y, 0.005f, 0.0f, 4.0f, "%.3f /m", nullptr,
				[&currentEntry](const float v) { currentEntry.set_ocean_absorption_g(v); });
			oceanFloat("Absorption B", currentEntry.has_ocean_absorption_b(), currentEntry.ocean_absorption_b(), defaults.absorption.z, 0.005f, 0.0f, 4.0f, "%.3f /m", nullptr,
				[&currentEntry](const float v) { currentEntry.set_ocean_absorption_b(v); });
			oceanFloat("Crest Glow", currentEntry.has_ocean_subsurface_strength(), currentEntry.ocean_subsurface_strength(), defaults.subsurfaceStrength, 0.02f, 0.0f, 10.0f, "%.2f",
				"Sunlight shining through the waves toward the viewer.",
				[&currentEntry](const float v) { currentEntry.set_ocean_subsurface_strength(v); });

			ImGui::Separator();
			ImGui::TextDisabled("Surface");
			oceanFloat("Roughness", currentEntry.has_ocean_roughness(), currentEntry.ocean_roughness(), defaults.roughness, 0.002f, 0.0f, 1.0f, "%.3f",
				"Blurs reflections and widens the sun glint.",
				[&currentEntry](const float v) { currentEntry.set_ocean_roughness(v); });
			oceanFloat("Reflection", currentEntry.has_ocean_reflection_strength(), currentEntry.ocean_reflection_strength(), defaults.reflectionStrength, 0.01f, 0.0f, 2.0f, "%.2f", nullptr,
				[&currentEntry](const float v) { currentEntry.set_ocean_reflection_strength(v); });
			oceanFloat("Foam Coverage", currentEntry.has_ocean_foam_coverage(), currentEntry.ocean_foam_coverage(), defaults.foamCoverage, 0.005f, -0.5f, 1.5f, "%.3f",
				"How readily breaking crests turn white.",
				[&currentEntry](const float v) { currentEntry.set_ocean_foam_coverage(v); });
			oceanFloat("Foam Brightness", currentEntry.has_ocean_foam_brightness(), currentEntry.ocean_foam_brightness(), defaults.foamBrightness, 0.01f, 0.0f, 2.0f, "%.2f", nullptr,
				[&currentEntry](const float v) { currentEntry.set_ocean_foam_brightness(v); });
			oceanFloat("Shore Foam", currentEntry.has_ocean_shore_foam_strength(), currentEntry.ocean_shore_foam_strength(), defaults.shoreFoamStrength, 0.01f, 0.0f, 2.0f, "%.2f", nullptr,
				[&currentEntry](const float v) { currentEntry.set_ocean_shore_foam_strength(v); });
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

			ImGui::Separator();
			ImGui::TextDisabled("Ambience");

			DrawSoundEntryCombo(m_project.sounds, "Calm Sound", currentEntry.ambient_calm_sound(), m_calmSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_ambient_calm_sound(id); });
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Looping bed heard near this liquid. None disables the ambience of this liquid.");
			}

			DrawSoundEntryCombo(m_project.sounds, "Fast Sound", currentEntry.ambient_fast_sound(), m_fastSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_ambient_fast_sound(id); });
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Looping bed crossfaded in as the water flows faster.");
			}

			bool shoreOnly = currentEntry.ambient_shore_only();
			if (ImGui::Checkbox("Shore Only", &shoreOnly))
			{
				currentEntry.set_ambient_shore_only(shoreOnly);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Only water bordering land is audible (shorelines) instead of the whole body.");
			}

			DrawSoundEntryCombo(m_project.sounds, "Swim Sound", currentEntry.ambient_swim_sound(), m_swimSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_ambient_swim_sound(id); });
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("2D loop layered in while the player swims in this liquid.");
			}

			DrawSoundEntryCombo(m_project.sounds, "Underwater Sound", currentEntry.ambient_underwater_sound(), m_underwaterSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_ambient_underwater_sound(id); });
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("2D loop played while the player's head is under this liquid. Replaces the swim sound while submerged.");
			}

			// The client clamps longer ranges anyway (water_ambience::MaxRange): the scan cost grows
			// with the square of the range.
			constexpr float maxAmbienceRange = 100.0f;
			float range = currentEntry.ambient_range();
			if (ImGui::DragFloat("Ambience Range", &range, 0.5f, 1.0f, maxAmbienceRange, "%.1f m"))
			{
				currentEntry.set_ambient_range(std::clamp(range, 1.0f, maxAmbienceRange));
			}

			float volume = currentEntry.ambient_volume();
			if (ImGui::DragFloat("Ambience Volume", &volume, 0.01f, 0.0f, 2.0f, "%.2f"))
			{
				currentEntry.set_ambient_volume(std::clamp(volume, 0.0f, 2.0f));
			}
		}
	}
}
