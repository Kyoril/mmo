// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_profile_editor_window.h"

#include "editor_imgui_helpers.h"
#include "environment_preview.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"

#include <algorithm>

#include <imgui/misc/cpp/imgui_stdlib.h>

namespace mmo
{
	namespace
	{
		/// Binds one day curve slot to its proto accessors and its Default curve.
		struct CurveSlot
		{
			const char* label;
			const char* alphaMeaning;
			bool (proto::EnvironmentProfile::*has)() const;
			const proto::ColorCurveData& (proto::EnvironmentProfile::*get)() const;
			proto::ColorCurveData* (proto::EnvironmentProfile::*mutableGet)();
			void (proto::EnvironmentProfile::*clear)();
			ColorCurve EnvironmentProfile::*defaultCurve;
		};

		const std::array<CurveSlot, EnvironmentProfileEditorWindow::CurveCount> s_curveSlots = { {
			{ "Sky Horizon", "unused", &proto::EnvironmentProfile::has_sky_horizon, &proto::EnvironmentProfile::sky_horizon, &proto::EnvironmentProfile::mutable_sky_horizon, &proto::EnvironmentProfile::clear_sky_horizon, &EnvironmentProfile::skyHorizon },
			{ "Sky Zenith", "unused", &proto::EnvironmentProfile::has_sky_zenith, &proto::EnvironmentProfile::sky_zenith, &proto::EnvironmentProfile::mutable_sky_zenith, &proto::EnvironmentProfile::clear_sky_zenith, &EnvironmentProfile::skyZenith },
			{ "Clouds", "unused", &proto::EnvironmentProfile::has_clouds, &proto::EnvironmentProfile::clouds, &proto::EnvironmentProfile::mutable_clouds, &proto::EnvironmentProfile::clear_clouds, &EnvironmentProfile::clouds },
			{ "Ambient", "unused", &proto::EnvironmentProfile::has_ambient, &proto::EnvironmentProfile::ambient, &proto::EnvironmentProfile::mutable_ambient, &proto::EnvironmentProfile::clear_ambient, &EnvironmentProfile::ambient },
			{ "Sun", "intensity", &proto::EnvironmentProfile::has_sun, &proto::EnvironmentProfile::sun, &proto::EnvironmentProfile::mutable_sun, &proto::EnvironmentProfile::clear_sun, &EnvironmentProfile::sun },
			{ "Moon", "intensity", &proto::EnvironmentProfile::has_moon, &proto::EnvironmentProfile::moon, &proto::EnvironmentProfile::mutable_moon, &proto::EnvironmentProfile::clear_moon, &EnvironmentProfile::moon },
			{ "Fog", "density multiplier", &proto::EnvironmentProfile::has_fog, &proto::EnvironmentProfile::fog, &proto::EnvironmentProfile::mutable_fog, &proto::EnvironmentProfile::clear_fog, &EnvironmentProfile::fog },
			{ "Sun Scatter (Shafts)", "shaft multiplier", &proto::EnvironmentProfile::has_sun_scatter, &proto::EnvironmentProfile::sun_scatter, &proto::EnvironmentProfile::mutable_sun_scatter, &proto::EnvironmentProfile::clear_sun_scatter, &EnvironmentProfile::sunScatter },
		} };

		std::unique_ptr<ColorCurveImGuiEditor> makeCurveEditor(const char* label, ColorCurve& curve)
		{
			auto editor = std::make_unique<ColorCurveImGuiEditor>(label, curve);
			editor->SetShowAlpha(true);
			editor->SetShowColorPreview(true);
			return editor;
		}
	}

	EnvironmentProfileEditorWindow::EnvironmentProfileEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.environmentProfiles, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Environment Profiles";
	}

	void EnvironmentProfileEditorWindow::OnNewEntry(proto::TemplateManager<proto::EnvironmentProfiles, proto::EnvironmentProfile>::EntryType& entry)
	{
		// Every curve starts empty (= Default) and every fixed value at its proto default, so a
		// new profile renders exactly like the built-in Default until something is changed.
		entry.set_name("New Environment");
		GetEnvironmentPreview().NotifyChanged();
	}

	bool EnvironmentProfileEditorWindow::CanRemoveEntry(const proto::EnvironmentProfile& entry) const
	{
		for (const auto& zone : m_project.zones.getTemplates().entry())
		{
			if (zone.environment_profile() == entry.id())
			{
				return false;
			}
		}

		for (const auto& map : m_project.maps.getTemplates().entry())
		{
			if (map.environment_profile() == entry.id())
			{
				return false;
			}
		}

		return true;
	}

	void EnvironmentProfileEditorWindow::OnEntryRemoved(const uint32 entryId)
	{
		EnvironmentPreview& preview = GetEnvironmentPreview();
		if (preview.profileId && *preview.profileId == entryId)
		{
			preview.profileId.reset();
			preview.NotifyChanged();
		}

		m_boundProfileId = 0;
	}

	void EnvironmentProfileEditorWindow::BindCurves(const proto::EnvironmentProfile& entry)
	{
		const EnvironmentProfile& defaults = *EnvironmentProfile::GetDefault();

		for (size_t i = 0; i < CurveCount; ++i)
		{
			const CurveSlot& slot = s_curveSlots[i];
			m_curves[i] = defaults.*slot.defaultCurve;
			LoadColorCurve((entry.*slot.get)(), m_curves[i]);
			m_curveEditors[i] = makeCurveEditor(slot.label, m_curves[i]);
		}

		m_boundProfileId = entry.id();
	}

	void EnvironmentProfileEditorWindow::DrawDetailsImpl(proto::EnvironmentProfile& currentEntry)
	{
		if (m_boundProfileId != currentEntry.id())
		{
			BindCurves(currentEntry);
		}

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

			DrawReferences(currentEntry);
		}

		DrawPreviewBar(currentEntry);
		DrawFixedValues(currentEntry);
		DrawWind(currentEntry);
		DrawCurves(currentEntry);
	}

	void EnvironmentProfileEditorWindow::DrawReferences(const proto::EnvironmentProfile& entry)
	{
		int count = 0;
		for (const auto& zone : m_project.zones.getTemplates().entry())
		{
			if (zone.environment_profile() == entry.id())
			{
				ImGui::BulletText("Zone: %s", zone.name().c_str());
				++count;
			}
		}

		for (const auto& map : m_project.maps.getTemplates().entry())
		{
			if (map.environment_profile() == entry.id())
			{
				ImGui::BulletText("Map default: %s", map.name().c_str());
				++count;
			}
		}

		if (count == 0)
		{
			ImGui::TextDisabled("Not used by any zone or map.");
		}
		else
		{
			ImGui::TextDisabled("Clear these references before removing the profile.");
		}
	}

	void EnvironmentProfileEditorWindow::DrawPreviewBar(const proto::EnvironmentProfile& entry)
	{
		if (const auto section = ScopedEditorSection("Preview", ImGuiTreeNodeFlags_DefaultOpen))
		{
			EnvironmentPreview& preview = GetEnvironmentPreview();

			bool previewing = preview.profileId && *preview.profileId == entry.id();
			if (ImGui::Checkbox("Preview in world editors", &previewing))
			{
				if (previewing)
				{
					preview.profileId = entry.id();
				}
				else
				{
					preview.profileId.reset();
				}
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Every open world editor shows this profile at the time below.\nTurn off to return to the zone under the camera and the editor's own clock.");
			}

			ImGui::BeginDisabled(!previewing);
			ImGui::SliderFloat("Time of Day", &preview.normalizedTime, 0.0f, 1.0f, "%.3f");

			if (ImGui::Button("Dawn (6:00)"))
			{
				preview.normalizedTime = 0.25f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Noon (12:00)"))
			{
				preview.normalizedTime = 0.5f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Dusk (18:00)"))
			{
				preview.normalizedTime = 0.75f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Midnight (0:00)"))
			{
				preview.normalizedTime = 0.0f;
			}
			ImGui::EndDisabled();
		}
	}

	void EnvironmentProfileEditorWindow::DrawFixedValues(proto::EnvironmentProfile& entry)
	{
		if (const auto section = ScopedEditorSection("Fog, Shafts and Post", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool changed = false;

			float density = entry.fog_density();
			if (ImGui::DragFloat("Fog Density", &density, 0.0005f, 0.0f, 1.0f, "%.4f"))
			{
				entry.set_fog_density(std::clamp(density, 0.0f, 1.0f));
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Extinction per metre at the base height. The Fog curve's alpha multiplies it over the day.");
			}

			float falloff = entry.fog_height_falloff();
			if (ImGui::DragFloat("Fog Height Falloff", &falloff, 0.001f, 0.0f, 1.0f, "%.3f"))
			{
				entry.set_fog_height_falloff(std::clamp(falloff, 0.0f, 1.0f));
				changed = true;
			}

			float baseHeight = entry.fog_base_height();
			if (ImGui::DragFloat("Fog Base Offset", &baseHeight, 0.5f, -10000.0f, 10000.0f, "%.1f"))
			{
				entry.set_fog_base_height(std::clamp(baseHeight, -10000.0f, 10000.0f));
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Fog base height relative to the player (client) / camera pivot (editor). Negative = below it.");
			}

			float anisotropy = entry.fog_anisotropy();
			if (ImGui::SliderFloat("Sun Glow Tightness", &anisotropy, 0.0f, 0.95f, "%.2f"))
			{
				entry.set_fog_anisotropy(anisotropy);
				changed = true;
			}

			float shaftStrength = entry.shaft_strength();
			if (ImGui::DragFloat("Light Shaft Strength", &shaftStrength, 0.01f, 0.0f, 16.0f, "%.2f"))
			{
				entry.set_shaft_strength(std::clamp(shaftStrength, 0.0f, 16.0f));
				changed = true;
			}

			float exposure = entry.exposure();
			if (ImGui::DragFloat("Exposure", &exposure, 0.01f, 0.1f, 8.0f, "%.2f"))
			{
				entry.set_exposure(std::clamp(exposure, 0.1f, 8.0f));
				changed = true;
			}

			float bloomIntensity = entry.bloom_intensity();
			if (ImGui::DragFloat("Bloom Intensity", &bloomIntensity, 0.005f, 0.0f, 1.0f, "%.3f"))
			{
				entry.set_bloom_intensity(std::clamp(bloomIntensity, 0.0f, 1.0f));
				changed = true;
			}

			float bloomThreshold = entry.bloom_threshold();
			if (ImGui::DragFloat("Bloom Threshold", &bloomThreshold, 0.01f, 0.0f, 16.0f, "%.2f"))
			{
				entry.set_bloom_threshold(std::clamp(bloomThreshold, 0.0f, 16.0f));
				changed = true;
			}

			float transition = entry.transition_seconds();
			if (ImGui::DragFloat("Transition Seconds", &transition, 0.1f, 0.0f, 30.0f, "%.1f"))
			{
				entry.set_transition_seconds(std::clamp(transition, 0.0f, 30.0f));
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("How long the fade into this profile takes when a player crosses into its zone. 0 snaps.");
			}

			if (changed)
			{
				GetEnvironmentPreview().NotifyChanged();
			}
		}
	}

	void EnvironmentProfileEditorWindow::DrawWind(proto::EnvironmentProfile& entry)
	{
		if (const auto section = ScopedEditorSection("Wind & Noise", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool changed = false;

			float direction = entry.wind_direction();
			if (ImGui::SliderFloat("Wind Direction", &direction, 0.0f, 360.0f, "%.0f deg"))
			{
				entry.set_wind_direction(std::clamp(direction, 0.0f, 360.0f));
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Direction the wind blows toward, clockwise from north (+Z). 90 = toward +X.");
			}

			float speed = entry.wind_speed();
			if (ImGui::DragFloat("Wind Speed", &speed, 0.05f, 0.0f, 30.0f, "%.1f m/s"))
			{
				entry.set_wind_speed(std::clamp(speed, 0.0f, 30.0f));
				changed = true;
			}

			float gustiness = entry.wind_gustiness();
			if (ImGui::SliderFloat("Gustiness", &gustiness, 0.0f, 1.0f, "%.2f"))
			{
				entry.set_wind_gustiness(gustiness);
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("How much gusts vary the wind speed (up to +/-60%%) and direction (up to +/-25 degrees).");
			}

			float noiseAmount = entry.fog_noise_amount();
			if (ImGui::SliderFloat("Fog Noise Amount", &noiseAmount, 0.0f, 1.0f, "%.2f"))
			{
				entry.set_fog_noise_amount(noiseAmount);
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("0 = smooth fog, 1 = drifting banks with clear gaps. Average fog amount stays the same.");
			}

			float noiseSize = entry.fog_noise_size();
			if (ImGui::DragFloat("Fog Noise Size", &noiseSize, 0.5f, 5.0f, 500.0f, "%.0f m"))
			{
				entry.set_fog_noise_size(std::clamp(noiseSize, 5.0f, 500.0f));
				changed = true;
			}

			if (changed)
			{
				GetEnvironmentPreview().NotifyChanged();
			}
		}
	}

	void EnvironmentProfileEditorWindow::DrawCurves(proto::EnvironmentProfile& entry)
	{
		const EnvironmentProfile& defaults = *EnvironmentProfile::GetDefault();

		for (size_t i = 0; i < CurveCount; ++i)
		{
			const CurveSlot& slot = s_curveSlots[i];
			ImGui::PushID(static_cast<int>(i));

			const bool authored = (entry.*slot.has)() && (entry.*slot.get)().key_size() > 0;
			const String header = String(slot.label) + (authored ? "" : "  (Default)") + "###curve";

			if (ImGui::CollapsingHeader(header.c_str()))
			{
				ImGui::TextDisabled("rgb: colour, alpha: %s", slot.alphaMeaning);

				ImGui::BeginDisabled(!authored);
				if (ImGui::Button("Reset to Default"))
				{
					(entry.*slot.clear)();
					m_curves[i] = defaults.*slot.defaultCurve;
					m_curveEditors[i] = makeCurveEditor(slot.label, m_curves[i]);
					GetEnvironmentPreview().NotifyChanged();
				}
				ImGui::EndDisabled();

				if (m_curveEditors[i]->Draw())
				{
					StoreColorCurve(m_curves[i], *(entry.*slot.mutableGet)());
					GetEnvironmentPreview().NotifyChanged();
				}
			}

			ImGui::PopID();
		}
	}
}
