// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_visualization_editor_window.h"
#include "asset_picker_widget.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "log/default_log_levels.h"
#include "preview_providers/preview_provider_manager.h"

namespace mmo
{
	// Event names matching proto::SpellVisualEvent enum
	static const char *s_eventNames[] = {
		"Start Cast",
		"Cancel Cast",
		"Casting",
		"Cast Succeeded",
		"Impact",
		"Aura Applied",
		"Aura Removed",
		"Aura Tick",
		"Aura Idle"};

	// Scope names matching proto::KitScope enum
	static const char *s_scopeNames[] = {
		"Caster",
		"Target",
		"Projectile Impact"};

	SpellVisualizationEditorWindow::SpellVisualizationEditorWindow(const String &name, proto::Project &project, EditorHost &host, PreviewProviderManager &previewManager, IAudio *audioSystem)
		: EditorEntryWindowBase<proto::SpellVisualizations, proto::SpellVisualization>(project, project.spellVisualizations, name), m_host(host), m_previewManager(previewManager), m_audioSystem(audioSystem)
	{
		m_hasToolbarButton = false;

		// Initialize all event sections as collapsed
		for (int i = 0; i < 9; ++i)
		{
			m_eventExpanded[i] = false;
		}
	}

	void SpellVisualizationEditorWindow::OnNewEntry(
		proto::TemplateManager<proto::SpellVisualizations, proto::SpellVisualization>::EntryType &entry)
	{
		EditorEntryWindowBase<proto::SpellVisualizations, proto::SpellVisualization>::OnNewEntry(entry);

		// Set default values for new entry
		entry.set_name("New Visualization");
		entry.set_icon("");
	}

	void SpellVisualizationEditorWindow::DrawDetailsImpl(proto::SpellVisualization &currentEntry)
	{
		// Basic fields
		ImGui::PushID(&currentEntry);

		ImGui::Separator();
		ImGui::Text("Basic Properties");
		ImGui::Separator();

		// ID (read-only display)
		ImGui::BeginDisabled();
		int id = currentEntry.id();
		ImGui::InputInt("ID", &id);
		ImGui::EndDisabled();

		// Name (required)
		std::string name = currentEntry.name();
		if (ImGui::InputText("Name", &name))
		{
			currentEntry.set_name(name);
		}
		if (name.empty())
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "(Required)");
		}

		// Icon path
		std::string icon = currentEntry.icon();
		static const std::set<String> iconExtensions = {".htex", ".blp"};
		if (AssetPickerWidget::Draw("Icon", icon, iconExtensions, &m_previewManager, nullptr, 64.0f))
		{
			currentEntry.set_icon(icon);
		}
		
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Visual Kits by Event");
		ImGui::Separator();
		ImGui::Spacing();

		// Draw kit editors for each event type
		for (int eventIdx = 0; eventIdx < 9; ++eventIdx)
		{
			DrawEventKits(currentEntry, eventIdx, s_eventNames[eventIdx]);
		}

		ImGui::PopID();
	}

	void SpellVisualizationEditorWindow::DrawEventKits(proto::SpellVisualization &currentEntry, uint32 eventValue, const char *eventName)
	{
		ImGui::PushID(eventValue);

		// Get or create the kit list for this event
		auto &kitsMap = *currentEntry.mutable_kits_by_event();

		// Check if we have kits for this event
		bool hasKits = kitsMap.find(eventValue) != kitsMap.end();
		const int kitCount = hasKits ? kitsMap[eventValue].kits_size() : 0;

		// Collapsing header with kit count
		char headerLabel[128];
		snprintf(headerLabel, sizeof(headerLabel), "%s (%d kits)###Event%u", eventName, kitCount, eventValue);

		if (ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();

			// Add kit button with unique label
			char addButtonLabel[64];
			snprintf(addButtonLabel, sizeof(addButtonLabel), "Add Kit##AddKit%u", eventValue);
			if (ImGui::Button(addButtonLabel))
			{
				// Create or get the kit list
				auto *newKit = kitsMap[eventValue].add_kits();
				newKit->set_scope(proto::CASTER);
				newKit->set_loop(false);
			}

			// Draw existing kits (re-fetch the list after potential add)
			if (kitsMap.find(eventValue) != kitsMap.end())
			{
				auto &kitList = kitsMap[eventValue];
				std::vector<int> kitsToRemove;

				for (int i = 0; i < kitList.kits_size(); ++i)
				{
					auto *kit = kitList.mutable_kits(i);
					if (DrawKit(*kit, i))
					{
						kitsToRemove.push_back(i);
					}
				}

				// Remove kits marked for deletion (in reverse order to maintain indices)
				for (auto it = kitsToRemove.rbegin(); it != kitsToRemove.rend(); ++it)
				{
					kitList.mutable_kits()->erase(kitList.mutable_kits()->begin() + *it);
				}

				// Clean up empty kit lists
				if (kitList.kits_size() == 0)
				{
					kitsMap.erase(eventValue);
				}
			}

			ImGui::Unindent();
		}

		ImGui::PopID();
	}

	bool SpellVisualizationEditorWindow::DrawKit(proto::SpellKit &kit, int kitIndex)
	{
		bool shouldRemove = false;

		ImGui::PushID(kitIndex);
		ImGui::Separator();

		char kitLabel[64];
		snprintf(kitLabel, sizeof(kitLabel), "Kit %d", kitIndex + 1);

		if (ImGui::TreeNode(kitLabel))
		{
			// Scope dropdown
			int scopeIndex = kit.has_scope() ? static_cast<int>(kit.scope()) : 0;
			if (ImGui::Combo("Scope", &scopeIndex, s_scopeNames, IM_ARRAYSIZE(s_scopeNames)))
			{
				kit.set_scope(static_cast<proto::KitScope>(scopeIndex));
			}

			// Animation name
			std::string animName = kit.has_animation_name() ? kit.animation_name() : "";
			if (ImGui::InputText("Animation Name", &animName))
			{
				kit.set_animation_name(animName);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?"))
			{
				ImGui::SetTooltip("Examples: CastLoop, CastRelease, SpellCast, etc.");
			}

			// Loop checkbox
			bool loop = kit.has_loop() && kit.loop();
			if (ImGui::Checkbox("Loop", &loop))
			{
				kit.set_loop(loop);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("??"))
			{
				ImGui::SetTooltip("Looped sounds will play continuously until the event ends (e.g., during Casting).");
			}

			// Duration
			int duration = kit.has_duration_ms() ? kit.duration_ms() : 0;
			if (ImGui::InputInt("Duration (ms)", &duration))
			{
				kit.set_duration_ms(duration);
			}

			// Sounds list
			ImGui::Spacing();
			if (ImGui::TreeNode("Sounds"))
			{
				// Add sound button
				if (ImGui::Button("Add Sound"))
				{
					kit.add_sounds("Sound/Spells/NewSound.wav");
				}

				// Draw existing sounds
				static const std::set<String> soundExtensions = {".wav", ".ogg", ".mp3"};
				std::vector<int> soundsToRemove;

				for (int i = 0; i < kit.sounds_size(); ++i)
				{
					ImGui::PushID(i);

					std::string sound = kit.sounds(i);
					char soundLabel[32];
					snprintf(soundLabel, sizeof(soundLabel), "Sound %d", i);

					// Use asset picker for sound selection
					if (AssetPickerWidget::Draw(soundLabel, sound, soundExtensions, nullptr, m_audioSystem, 0.0f))
					{
						kit.set_sounds(i, sound);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Remove"))
					{
						soundsToRemove.push_back(i);
					}

					ImGui::PopID();
				}

				// Remove sounds (in reverse order)
				for (auto it = soundsToRemove.rbegin(); it != soundsToRemove.rend(); ++it)
				{
					kit.mutable_sounds()->erase(kit.mutable_sounds()->begin() + *it);
				}

				ImGui::TreePop();
			}

			// Tint color
			ImGui::Spacing();
			if (ImGui::TreeNode("Tint Color"))
			{
				auto *tint = kit.mutable_tint();

				float color[4] = {
					tint->has_r() ? tint->r() : 1.0f,
					tint->has_g() ? tint->g() : 1.0f,
					tint->has_b() ? tint->b() : 1.0f,
					tint->has_a() ? tint->a() : 1.0f};

				if (ImGui::ColorEdit4("RGBA", color))
				{
					tint->set_r(color[0]);
					tint->set_g(color[1]);
					tint->set_b(color[2]);
					tint->set_a(color[3]);
				}

				int tintDuration = tint->has_duration_ms() ? tint->duration_ms() : 0;
				if (ImGui::InputInt("Tint Duration (ms)", &tintDuration))
				{
					tint->set_duration_ms(tintDuration);
				}

				ImGui::TreePop();
			}

			// Remove kit button
			ImGui::Spacing();
			if (ImGui::Button("Remove This Kit"))
			{
				shouldRemove = true;
			}

			ImGui::TreePop();
		}
		else
		{
			// Draw compact view when collapsed
			ImGui::SameLine();
			const char *scopeName = kit.has_scope() ? s_scopeNames[kit.scope()] : "Caster";
			ImGui::TextDisabled("(%s, %d sounds)", scopeName, kit.sounds_size());
		}

		ImGui::PopID();
		return shouldRemove;
	}
}
