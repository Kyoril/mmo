// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_visualization_editor_window.h"
#include "asset_picker_widget.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "editor_imgui_helpers.h"
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

	// Motion type names matching proto::ProjectileMotion enum
	static const char *s_motionTypeNames[] = {
		"Linear",
		"Arc",
		"Homing",
		"Sine Wave"};

	SpellVisualizationEditorWindow::SpellVisualizationEditorWindow(const String &name, proto::Project &project, EditorHost &host, PreviewProviderManager &previewManager, IAudio *audioSystem)
		: EditorEntryWindowBase<proto::SpellVisualizations, proto::SpellVisualization>(project, project.spellVisualizations, name)
		, m_host(host)
		, m_previewManager(previewManager)
		, m_audioSystem(audioSystem)
	{
		m_visible = false;
		m_hasToolbarButton = false;

		// Initialize all event sections as collapsed
		for (int i = 0; i < 9; ++i)
		{
			m_eventExpanded[i] = false;
		}

		// Create the 3D preview panel with audio support
		m_preview = std::make_unique<SpellVisualizationPreview>(host, audioSystem);

		// Set initial projectile speed for the preview
		if (m_preview)
		{
			m_preview->SetProjectileSpeed(m_previewProjectileSpeed);
		}
	}

	bool SpellVisualizationEditorWindow::Draw()
	{
		if (!m_visible)
		{
			return false;
		}

		ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);

		if (ImGui::Begin(m_name.c_str(), &m_visible, ImGuiWindowFlags_MenuBar))
		{
			// Menu bar
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("View"))
				{
					ImGui::MenuItem("Show 3D Preview", nullptr, &m_showPreview);
					ImGui::EndMenu();
				}
				ImGui::EndMenuBar();
			}

			// Main layout: 3 columns - List | Details | Preview
			const float windowWidth = ImGui::GetContentRegionAvail().x;
			const float listWidth = 250.0f;
			const float minPreviewWidth = 450.0f;
			const float previewWidth = m_showPreview ? std::max(windowWidth * 0.40f, minPreviewWidth) : 0.0f;
			const float detailsWidth = windowWidth - listWidth - previewWidth - 16.0f;

			// Left column - Entry list
			ImGui::BeginChild("##entryListColumn", ImVec2(listWidth, 0), true);
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

				if (DrawPrimaryButton("+ Add New", ImVec2(-1, 0)))
				{
					auto *entry = m_manager.add();
					OnNewEntry(*entry);
					m_currentItem = m_manager.count() - 1;
				}

				ImGui::BeginDisabled(m_currentItem == -1);
				if (DrawDangerButton("Remove Selected", ImVec2(-1, 0)))
				{
					m_manager.remove(m_manager.getTemplates().entry().at(m_currentItem).id());
					m_currentItem = -1;
				}
				ImGui::EndDisabled();

				ImGui::Spacing();

				// Search bar
				static char searchBuffer[256] = "";
				ImGui::SetNextItemWidth(-1);
				ImGui::InputTextWithHint("##search", "Search...", searchBuffer, IM_ARRAYSIZE(searchBuffer));

				const String searchText = searchBuffer;
				std::string lowerSearchText = searchText.c_str();
				std::transform(lowerSearchText.begin(), lowerSearchText.end(), lowerSearchText.begin(),
					[](unsigned char c) { return std::tolower(c); });

				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%zu entries", m_manager.count());
				ImGui::Spacing();

				// Entry list
				ImGui::BeginChild("##entryListScroll", ImVec2(0, 0));
				for (int i = 0; i < m_manager.count(); ++i)
				{
					const auto& entry = m_manager.getTemplates().entry().at(i);
					
					// Filter by search
					if (!lowerSearchText.empty())
					{
						std::string lowerName = entry.name();
						std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
							[](unsigned char c) { return std::tolower(c); });
						std::string idStr = std::to_string(entry.id());
						
						if (lowerName.find(lowerSearchText) == std::string::npos &&
							idStr.find(lowerSearchText) == std::string::npos)
						{
							continue;
						}
					}

					char label[256];
					snprintf(label, sizeof(label), "%u: %s", entry.id(), entry.name().c_str());

					if (ImGui::Selectable(label, m_currentItem == i))
					{
						m_currentItem = i;
						
						// Sync preview with new selection
						if (m_preview && m_currentItem >= 0)
						{
							auto& currentEntry = m_manager.getTemplates().mutable_entry()->at(m_currentItem);
							m_preview->SyncWithVisualization(currentEntry);
						}
					}
				}
				ImGui::EndChild();

				ImGui::PopStyleVar(2);
			}
			ImGui::EndChild();

			ImGui::SameLine();

			// Middle column - Details
			ImGui::BeginChild("##detailsColumn", ImVec2(detailsWidth, 0), true);
			{
				if (m_currentItem >= 0 && m_currentItem < m_manager.count())
				{
					auto* currentEntry = &m_manager.getTemplates().mutable_entry()->at(m_currentItem);
					
					// Quick action toolbar at top
					DrawQuickActions(*currentEntry);
					
					ImGui::Separator();
					
					// Scrollable details
					ImGui::BeginChild("##detailsScroll", ImVec2(0, 0));
					DrawDetailsImpl(*currentEntry);
					ImGui::EndChild();
				}
				else
				{
					ImGui::TextDisabled("Select a visualization from the list");
				}
			}
			ImGui::EndChild();

			// Right column - 3D Preview
			if (m_showPreview)
			{
				ImGui::SameLine();

				ImGui::BeginChild("##previewColumn", ImVec2(0, 0), true);
				{
					proto::SpellVisualization* currentEntry = nullptr;
					if (m_currentItem >= 0 && m_currentItem < m_manager.count())
					{
						currentEntry = &m_manager.getTemplates().mutable_entry()->at(m_currentItem);
					}

					DrawPreviewPanel(currentEntry);
				}
				ImGui::EndChild();
			}
		}
		ImGui::End();

		return false;
	}

	void SpellVisualizationEditorWindow::DrawQuickActions(proto::SpellVisualization& currentEntry)
	{
		// Quick actions are now integrated into the preview panel toolbar
		// This method is kept for potential future quick action buttons in the details panel
	}

	void SpellVisualizationEditorWindow::DrawPreviewPanel(proto::SpellVisualization* currentEntry)
	{
		if (!m_preview)
		{
			ImGui::TextDisabled("Preview not available");
			return;
		}

		// Preview toolbar
		m_preview->DrawToolbar(currentEntry);
		
		ImGui::Separator();

		// 3D viewport fills remaining space
		m_preview->DrawViewport(currentEntry, "##spellPreviewViewport");
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

		// Effect preset path (.hsep file for 3D preview)
		// Sync preview with current entry when selection changes
		if (m_preview)
		{
			m_preview->SyncWithVisualization(currentEntry);
		}

		ImGui::Text("Visual Kits by Event");
		ImGui::Separator();
		ImGui::Spacing();

		// Draw kit editors for each event type
		for (int eventIdx = 0; eventIdx < 9; ++eventIdx)
		{
			DrawEventKits(currentEntry, eventIdx, s_eventNames[eventIdx]);
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Projectile Configuration");
		ImGui::Separator();
		ImGui::Spacing();

		// Draw projectile configuration
		DrawProjectileConfig(currentEntry);

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

			// Bone attachment name
			std::string attachBone = kit.has_attach_bone() ? kit.attach_bone() : "";
			if (ImGui::InputText("Attach Bone", &attachBone))
			{
				kit.set_attach_bone(attachBone);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##bone"))
			{
				ImGui::SetTooltip("Bone/socket name to attach effects to (e.g., hand_right, hand_left, chest).\nLeave empty to use the entity root.");
			}

			// Animation name
			std::string animName = kit.has_animation_name() ? kit.animation_name() : "";
			if (ImGui::InputText("Animation Name", &animName))
			{
				kit.set_animation_name(animName);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##anim"))
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
			if (ImGui::SmallButton("?##loop"))
			{
				ImGui::SetTooltip("Looped effects play continuously until the event ends (e.g., during Casting).\nApplies to animation, sounds, particles, light, and ribbon trail.");
			}

			// Duration
			int duration = kit.has_duration_ms() ? kit.duration_ms() : 0;
			if (ImGui::InputInt("Duration (ms)", &duration))
			{
				kit.set_duration_ms(duration);
			}

			// --- Sounds ---
			ImGui::Spacing();
			if (ImGui::TreeNode("Sounds"))
			{
				if (ImGui::Button("Add Sound"))
				{
					kit.add_sounds("Sound/Spells/NewSound.wav");
				}

				static const std::set<String> soundExtensions = {".wav", ".ogg", ".mp3"};
				std::vector<int> soundsToRemove;

				for (int i = 0; i < kit.sounds_size(); ++i)
				{
					ImGui::PushID(i);

					std::string sound = kit.sounds(i);
					char soundLabel[32];
					snprintf(soundLabel, sizeof(soundLabel), "Sound %d", i);

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

				for (auto it = soundsToRemove.rbegin(); it != soundsToRemove.rend(); ++it)
				{
					kit.mutable_sounds()->erase(kit.mutable_sounds()->begin() + *it);
				}

				ImGui::TreePop();
			}

			// --- Particles ---
			ImGui::Spacing();
			if (ImGui::TreeNode("Particles"))
			{
				if (ImGui::Button("Add Particle System"))
				{
					kit.add_particles("");
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("?##particles"))
				{
					ImGui::SetTooltip("Particle systems to spawn for this kit.\nIf Attach Bone is set, particles are attached to that bone via TagPoints.");
				}

				static const std::set<String> particleExtensions = {".hpfx"};
				std::vector<int> particlesToRemove;

				for (int i = 0; i < kit.particles_size(); ++i)
				{
					ImGui::PushID(i);

					std::string particle = kit.particles(i);
					char particleLabel[32];
					snprintf(particleLabel, sizeof(particleLabel), "Particle %d", i);

					if (AssetPickerWidget::Draw(particleLabel, particle, particleExtensions, &m_previewManager, nullptr, 64.0f))
					{
						kit.set_particles(i, particle);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Remove"))
					{
						particlesToRemove.push_back(i);
					}

					ImGui::PopID();
				}

				for (auto it = particlesToRemove.rbegin(); it != particlesToRemove.rend(); ++it)
				{
					kit.mutable_particles()->erase(kit.mutable_particles()->begin() + *it);
				}

				ImGui::TreePop();
			}

			// --- Mesh ---
			ImGui::Spacing();
			if (ImGui::TreeNode("Mesh"))
			{
				std::string meshName = kit.has_mesh_name() ? kit.mesh_name() : "";
				static const std::set<String> meshExtensions = {".hmsh"};
				if (AssetPickerWidget::Draw("Mesh", meshName, meshExtensions, &m_previewManager, nullptr, 64.0f))
				{
					kit.set_mesh_name(meshName);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("?##mesh"))
				{
					ImGui::SetTooltip("Mesh to spawn at bone/position (e.g., a glow orb or magic circle).\nIf Attach Bone is set, mesh is attached to that bone via TagPoints.");
				}

				ImGui::TreePop();
			}

			// --- Point Light ---
			ImGui::Spacing();
			if (ImGui::TreeNode("Point Light"))
			{
				bool hasLight = kit.has_light();
				if (ImGui::Checkbox("Enable Light", &hasLight))
				{
					if (hasLight)
					{
						auto* light = kit.mutable_light();
						light->set_r(1.0f);
						light->set_g(0.9f);
						light->set_b(0.7f);
						light->set_intensity(1.0f);
						light->set_range(10.0f);
					}
					else
					{
						kit.clear_light();
					}
				}

				if (kit.has_light())
				{
					auto* light = kit.mutable_light();

					float lightColor[3] =
					{
						light->has_r() ? light->r() : 1.0f,
						light->has_g() ? light->g() : 0.9f,
						light->has_b() ? light->b() : 0.7f
					};
					if (ImGui::ColorEdit3("Color", lightColor))
					{
						light->set_r(lightColor[0]);
						light->set_g(lightColor[1]);
						light->set_b(lightColor[2]);
					}

					float intensity = light->has_intensity() ? light->intensity() : 1.0f;
					if (ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 20.0f))
					{
						light->set_intensity(intensity);
					}

					float range = light->has_range() ? light->range() : 10.0f;
					if (ImGui::DragFloat("Range", &range, 0.5f, 0.1f, 100.0f))
					{
						light->set_range(range);
					}

					float attenuation = light->has_attenuation() ? light->attenuation() : 1.0f;
					if (ImGui::DragFloat("Attenuation", &attenuation, 0.1f, 0.0f, 5.0f))
					{
						light->set_attenuation(attenuation);
					}

					float fadeIn = light->has_fade_in_time() ? light->fade_in_time() : 0.3f;
					if (ImGui::DragFloat("Fade In Time", &fadeIn, 0.05f, 0.0f, 5.0f, "%.2f s"))
					{
						light->set_fade_in_time(fadeIn);
					}

					float fadeOut = light->has_fade_out_time() ? light->fade_out_time() : 0.5f;
					if (ImGui::DragFloat("Fade Out Time", &fadeOut, 0.05f, 0.0f, 5.0f, "%.2f s"))
					{
						light->set_fade_out_time(fadeOut);
					}

					ImGui::SameLine();
					if (ImGui::SmallButton("?##light"))
					{
						ImGui::SetTooltip("Point light at bone/entity position.\nGreat for glowing hands, fiery aura effects, etc.");
					}
				}

				ImGui::TreePop();
			}

			// --- Ribbon Trail ---
			ImGui::Spacing();
			if (ImGui::TreeNode("Ribbon Trail"))
			{
				bool hasRibbon = kit.has_ribbon_trail();
				if (ImGui::Checkbox("Enable Ribbon Trail", &hasRibbon))
				{
					if (hasRibbon)
					{
						auto* ribbon = kit.mutable_ribbon_trail();
						ribbon->set_initial_width(0.5f);
						ribbon->set_final_width(0.0f);
						ribbon->set_segment_lifetime(1.0f);
						ribbon->set_max_segments(64);
					}
					else
					{
						kit.clear_ribbon_trail();
					}
				}

				if (kit.has_ribbon_trail())
				{
					auto* ribbon = kit.mutable_ribbon_trail();

					// Material
					std::string ribbonMaterial = ribbon->has_material_name() ? ribbon->material_name() : "";
					static const std::set<String> materialExtensions = {".hmat"};
					if (AssetPickerWidget::Draw("Material", ribbonMaterial, materialExtensions, nullptr, nullptr, 0.0f))
					{
						ribbon->set_material_name(ribbonMaterial);
					}

					// Width
					float initWidth = ribbon->has_initial_width() ? ribbon->initial_width() : 0.5f;
					if (ImGui::DragFloat("Initial Width", &initWidth, 0.01f, 0.0f, 10.0f))
					{
						ribbon->set_initial_width(initWidth);
					}

					float finalWidth = ribbon->has_final_width() ? ribbon->final_width() : 0.0f;
					if (ImGui::DragFloat("Final Width", &finalWidth, 0.01f, 0.0f, 10.0f))
					{
						ribbon->set_final_width(finalWidth);
					}

					// Initial color
					float initColor[4] =
					{
						ribbon->has_initial_r() ? ribbon->initial_r() : 1.0f,
						ribbon->has_initial_g() ? ribbon->initial_g() : 1.0f,
						ribbon->has_initial_b() ? ribbon->initial_b() : 1.0f,
						ribbon->has_initial_a() ? ribbon->initial_a() : 1.0f
					};
					if (ImGui::ColorEdit4("Initial Color", initColor))
					{
						ribbon->set_initial_r(initColor[0]);
						ribbon->set_initial_g(initColor[1]);
						ribbon->set_initial_b(initColor[2]);
						ribbon->set_initial_a(initColor[3]);
					}

					// Final color
					float finalColor[4] =
					{
						ribbon->has_final_r() ? ribbon->final_r() : 1.0f,
						ribbon->has_final_g() ? ribbon->final_g() : 1.0f,
						ribbon->has_final_b() ? ribbon->final_b() : 1.0f,
						ribbon->has_final_a() ? ribbon->final_a() : 0.0f
					};
					if (ImGui::ColorEdit4("Final Color", finalColor))
					{
						ribbon->set_final_r(finalColor[0]);
						ribbon->set_final_g(finalColor[1]);
						ribbon->set_final_b(finalColor[2]);
						ribbon->set_final_a(finalColor[3]);
					}

					// Timing
					float segLifetime = ribbon->has_segment_lifetime() ? ribbon->segment_lifetime() : 1.0f;
					if (ImGui::DragFloat("Segment Lifetime", &segLifetime, 0.05f, 0.1f, 10.0f))
					{
						ribbon->set_segment_lifetime(segLifetime);
					}

					int maxSeg = ribbon->has_max_segments() ? static_cast<int>(ribbon->max_segments()) : 64;
					if (ImGui::InputInt("Max Segments", &maxSeg))
					{
						if (maxSeg < 4)
						{
							maxSeg = 4;
						}
						ribbon->set_max_segments(static_cast<uint32_t>(maxSeg));
					}
				}

				ImGui::TreePop();
			}

			// --- Tint Color ---
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
			// Draw compact summary when collapsed
			ImGui::SameLine();
			const char *scopeName = kit.has_scope() ? s_scopeNames[kit.scope()] : "Caster";
			const char* boneName = kit.has_attach_bone() && !kit.attach_bone().empty() ? kit.attach_bone().c_str() : "root";
			ImGui::TextDisabled("(%s @ %s | %d snd, %d pfx%s%s%s)",
				scopeName, boneName,
				kit.sounds_size(), kit.particles_size(),
				kit.has_light() ? " +light" : "",
				kit.has_ribbon_trail() ? " +ribbon" : "",
				kit.has_mesh_name() && !kit.mesh_name().empty() ? " +mesh" : "");
		}

		ImGui::PopID();
		return shouldRemove;
	}

	void SpellVisualizationEditorWindow::DrawProjectileConfig(proto::SpellVisualization &currentEntry)
	{
		ImGui::PushID("ProjectileConfig");

		// Check if projectile config exists
		bool hasProjectile = currentEntry.has_projectile();

		if (ImGui::Checkbox("Enable Projectile", &hasProjectile))
		{
			if (hasProjectile)
			{
				// Create projectile config with defaults
				auto *projectile = currentEntry.mutable_projectile();
				projectile->set_motion(proto::LINEAR);
				projectile->set_scale(1.0f);
				projectile->set_face_movement(true);
			}
			else
			{
				// Clear projectile config
				currentEntry.clear_projectile();
			}
		}

		if (!hasProjectile)
		{
			ImGui::PopID();
			return;
		}

		ImGui::Indent();
		auto *projectile = currentEntry.mutable_projectile();

		// Spawn bone
		std::string spawnBone = projectile->has_spawn_bone() ? projectile->spawn_bone() : "";
		if (ImGui::InputText("Spawn Bone", &spawnBone))
		{
			projectile->set_spawn_bone(spawnBone);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("?##spawnbone"))
		{
			ImGui::SetTooltip("Bone name to spawn the projectile from (e.g., hand_right).\nLeave empty to use a default offset above the caster.");
		}

		// Preview speed setting (affects editor preview only)
		ImGui::TextDisabled("Preview Settings (Editor Only)");
		if (ImGui::DragFloat("Preview Speed", &m_previewProjectileSpeed, 0.5f, 1.0f, 100.0f, "%.1f units/sec"))
		{
			// Update the preview speed
			if (m_preview)
			{
				m_preview->SetProjectileSpeed(m_previewProjectileSpeed);
			}
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("?##previewspeed"))
		{
			ImGui::SetTooltip("Speed for previewing projectiles in the editor.\nActual speed is determined by the Spell entry that uses this visualization.");
		}
		ImGui::Separator();

		// Movement section
		if (ImGui::CollapsingHeader("Movement", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			// Motion type
			int motionType = projectile->has_motion() ? static_cast<int>(projectile->motion()) : 0;
			if (ImGui::Combo("Motion Type", &motionType, s_motionTypeNames, IM_ARRAYSIZE(s_motionTypeNames)))
			{
				projectile->set_motion(static_cast<proto::ProjectileMotion>(motionType));
			}

			// Motion-specific parameters
			switch (projectile->motion())
			{
			case proto::ARC:
				{
					float arcHeight = projectile->has_arc_height() ? projectile->arc_height() : 0.0f;
					if (ImGui::DragFloat("Arc Height", &arcHeight, 0.1f, 0.0f, 50.0f))
					{
						projectile->set_arc_height(arcHeight);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("?##arc"))
					{
						ImGui::SetTooltip("Maximum height of the parabolic arc in world units");
					}
					break;
				}
			case proto::HOMING:
				{
					float homingStrength = projectile->has_homing_strength() ? projectile->homing_strength() : 5.0f;
					if (ImGui::DragFloat("Homing Strength", &homingStrength, 0.1f, 0.1f, 20.0f))
					{
						projectile->set_homing_strength(homingStrength);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("?##homing"))
					{
						ImGui::SetTooltip("Turn rate - higher values make sharper turns");
					}
					break;
				}
			case proto::SINE_WAVE:
				{
					float frequency = projectile->has_wave_frequency() ? projectile->wave_frequency() : 1.0f;
					if (ImGui::DragFloat("Wave Frequency", &frequency, 0.1f, 0.1f, 10.0f))
					{
						projectile->set_wave_frequency(frequency);
					}

					float amplitude = projectile->has_wave_amplitude() ? projectile->wave_amplitude() : 1.0f;
					if (ImGui::DragFloat("Wave Amplitude", &amplitude, 0.1f, 0.0f, 10.0f))
					{
						projectile->set_wave_amplitude(amplitude);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("?##sine"))
					{
						ImGui::SetTooltip("Side-to-side oscillation distance");
					}
					break;
				}
			default:
				break;
			}

			ImGui::Unindent();
		}

		// Visual representation section
		if (ImGui::CollapsingHeader("Visual Representation", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			// Mesh name
			std::string meshName = projectile->has_mesh_name() ? projectile->mesh_name() : "";
			static const std::set<String> meshExtensions = {".hmsh"};
			if (AssetPickerWidget::Draw("Mesh", meshName, meshExtensions, &m_previewManager, nullptr, 64.0f))
			{
				projectile->set_mesh_name(meshName);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##mesh"))
			{
				ImGui::SetTooltip("3D mesh for the projectile (e.g., arrow, fireball orb)");
			}

			// Material name
			std::string materialName = projectile->has_material_name() ? projectile->material_name() : "";
			static const std::set<String> materialExtensions = {".hmat"};
			if (AssetPickerWidget::Draw("Material", materialName, materialExtensions, nullptr, nullptr, 0.0f))
			{
				projectile->set_material_name(materialName);
			}

			// Trail particle
			std::string trailParticle = projectile->has_trail_particle() ? projectile->trail_particle() : "";
			static const std::set<String> particleExtensions = {".hpfx"};
			if (AssetPickerWidget::Draw("Trail Particle", trailParticle, particleExtensions, &m_previewManager, nullptr, 64.0f))
			{
				projectile->set_trail_particle(trailParticle);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##trail"))
			{
				ImGui::SetTooltip("Particle system for trailing effect behind the projectile");
			}

			// Scale
			float scale = projectile->has_scale() ? projectile->scale() : 1.0f;
			if (ImGui::DragFloat("Scale", &scale, 0.01f, 0.1f, 10.0f))
			{
				projectile->set_scale(scale);
			}

			ImGui::Unindent();
		}

		// Rotation section
		if (ImGui::CollapsingHeader("Rotation"))
		{
			ImGui::Indent();

			// Face movement
			bool faceMovement = projectile->has_face_movement() ? projectile->face_movement() : true;
			if (ImGui::Checkbox("Face Movement Direction", &faceMovement))
			{
				projectile->set_face_movement(faceMovement);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##face"))
			{
				ImGui::SetTooltip("Automatically orient projectile along velocity vector");
			}

			// Spin rate
			float spinRate = projectile->has_spin_rate() ? projectile->spin_rate() : 0.0f;
			if (ImGui::DragFloat("Spin Rate (deg/sec)", &spinRate, 1.0f, -720.0f, 720.0f))
			{
				projectile->set_spin_rate(spinRate);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##spin"))
			{
				ImGui::SetTooltip("Rotation around forward axis in degrees per second");
			}

			ImGui::Unindent();
		}

		// Point Light on projectile
		if (ImGui::CollapsingHeader("Projectile Light"))
		{
			ImGui::Indent();

			bool hasProjLight = projectile->has_light();
			if (ImGui::Checkbox("Enable Projectile Light", &hasProjLight))
			{
				if (hasProjLight)
				{
					auto* light = projectile->mutable_light();
					light->set_r(1.0f);
					light->set_g(0.6f);
					light->set_b(0.2f);
					light->set_intensity(2.0f);
					light->set_range(8.0f);
				}
				else
				{
					projectile->clear_light();
				}
			}

			if (projectile->has_light())
			{
				auto* light = projectile->mutable_light();

				float lightColor[3] =
				{
					light->has_r() ? light->r() : 1.0f,
					light->has_g() ? light->g() : 0.6f,
					light->has_b() ? light->b() : 0.2f
				};
				if (ImGui::ColorEdit3("Color", lightColor))
				{
					light->set_r(lightColor[0]);
					light->set_g(lightColor[1]);
					light->set_b(lightColor[2]);
				}

				float intensity = light->has_intensity() ? light->intensity() : 2.0f;
				if (ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 20.0f))
				{
					light->set_intensity(intensity);
				}

				float range = light->has_range() ? light->range() : 8.0f;
				if (ImGui::DragFloat("Range", &range, 0.5f, 0.1f, 100.0f))
				{
					light->set_range(range);
				}

				float fadeIn = light->has_fade_in_time() ? light->fade_in_time() : 0.3f;
				if (ImGui::DragFloat("Fade In Time##proj", &fadeIn, 0.05f, 0.0f, 5.0f, "%.2f s"))
				{
					light->set_fade_in_time(fadeIn);
				}

				float fadeOut = light->has_fade_out_time() ? light->fade_out_time() : 0.5f;
				if (ImGui::DragFloat("Fade Out Time##proj", &fadeOut, 0.05f, 0.0f, 5.0f, "%.2f s"))
				{
					light->set_fade_out_time(fadeOut);
				}
			}

			ImGui::Unindent();
		}

		// Ribbon Trail on projectile
		if (ImGui::CollapsingHeader("Projectile Ribbon Trail"))
		{
			ImGui::Indent();

			bool hasProjRibbon = projectile->has_ribbon_trail();
			if (ImGui::Checkbox("Enable Projectile Ribbon Trail", &hasProjRibbon))
			{
				if (hasProjRibbon)
				{
					auto* ribbon = projectile->mutable_ribbon_trail();
					ribbon->set_initial_width(0.3f);
					ribbon->set_final_width(0.0f);
					ribbon->set_segment_lifetime(0.8f);
					ribbon->set_max_segments(48);
					ribbon->set_initial_r(1.0f);
					ribbon->set_initial_g(0.3f);
					ribbon->set_initial_b(0.1f);
					ribbon->set_initial_a(1.0f);
					ribbon->set_final_r(1.0f);
					ribbon->set_final_g(0.1f);
					ribbon->set_final_b(0.0f);
					ribbon->set_final_a(0.0f);
				}
				else
				{
					projectile->clear_ribbon_trail();
				}
			}

			if (projectile->has_ribbon_trail())
			{
				auto* ribbon = projectile->mutable_ribbon_trail();

				// Material
				std::string ribbonMaterial = ribbon->has_material_name() ? ribbon->material_name() : "";
				static const std::set<String> ribbonMatExtensions = {".hmat"};
				if (AssetPickerWidget::Draw("Material", ribbonMaterial, ribbonMatExtensions, nullptr, nullptr, 0.0f))
				{
					ribbon->set_material_name(ribbonMaterial);
				}

				// Width
				float initWidth = ribbon->has_initial_width() ? ribbon->initial_width() : 0.3f;
				if (ImGui::DragFloat("Initial Width", &initWidth, 0.01f, 0.0f, 10.0f))
				{
					ribbon->set_initial_width(initWidth);
				}

				float finalWidth = ribbon->has_final_width() ? ribbon->final_width() : 0.0f;
				if (ImGui::DragFloat("Final Width", &finalWidth, 0.01f, 0.0f, 10.0f))
				{
					ribbon->set_final_width(finalWidth);
				}

				// Initial color
				float initColor[4] =
				{
					ribbon->has_initial_r() ? ribbon->initial_r() : 1.0f,
					ribbon->has_initial_g() ? ribbon->initial_g() : 0.3f,
					ribbon->has_initial_b() ? ribbon->initial_b() : 0.1f,
					ribbon->has_initial_a() ? ribbon->initial_a() : 1.0f
				};
				if (ImGui::ColorEdit4("Initial Color", initColor))
				{
					ribbon->set_initial_r(initColor[0]);
					ribbon->set_initial_g(initColor[1]);
					ribbon->set_initial_b(initColor[2]);
					ribbon->set_initial_a(initColor[3]);
				}

				// Final color
				float finalColor[4] =
				{
					ribbon->has_final_r() ? ribbon->final_r() : 1.0f,
					ribbon->has_final_g() ? ribbon->final_g() : 0.1f,
					ribbon->has_final_b() ? ribbon->final_b() : 0.0f,
					ribbon->has_final_a() ? ribbon->final_a() : 0.0f
				};
				if (ImGui::ColorEdit4("Final Color", finalColor))
				{
					ribbon->set_final_r(finalColor[0]);
					ribbon->set_final_g(finalColor[1]);
					ribbon->set_final_b(finalColor[2]);
					ribbon->set_final_a(finalColor[3]);
				}

				// Timing
				float segLifetime = ribbon->has_segment_lifetime() ? ribbon->segment_lifetime() : 0.8f;
				if (ImGui::DragFloat("Segment Lifetime", &segLifetime, 0.05f, 0.1f, 10.0f))
				{
					ribbon->set_segment_lifetime(segLifetime);
				}

				int maxSeg = ribbon->has_max_segments() ? static_cast<int>(ribbon->max_segments()) : 48;
				if (ImGui::InputInt("Max Segments", &maxSeg))
				{
					if (maxSeg < 4)
					{
						maxSeg = 4;
					}
					ribbon->set_max_segments(static_cast<uint32_t>(maxSeg));
				}
			}

			ImGui::Unindent();
		}

		// Effects section
		if (ImGui::CollapsingHeader("Effects"))
		{
			ImGui::Indent();

			// Flight sounds
			if (ImGui::TreeNode("Flight Sounds"))
			{
				if (ImGui::Button("Add Sound"))
				{
					projectile->add_sounds("Sound/Spells/Projectile.wav");
				}

				static const std::set<String> soundExtensions = {".wav", ".ogg", ".mp3"};
				std::vector<int> soundsToRemove;

				for (int i = 0; i < projectile->sounds_size(); ++i)
				{
					ImGui::PushID(i);

					std::string sound = projectile->sounds(i);
					char soundLabel[32];
					snprintf(soundLabel, sizeof(soundLabel), "Sound %d", i);

					if (AssetPickerWidget::Draw(soundLabel, sound, soundExtensions, nullptr, m_audioSystem, 0.0f))
					{
						projectile->set_sounds(i, sound);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Remove"))
					{
						soundsToRemove.push_back(i);
					}

					ImGui::PopID();
				}

				// Remove sounds in reverse order
				for (auto it = soundsToRemove.rbegin(); it != soundsToRemove.rend(); ++it)
				{
					projectile->mutable_sounds()->erase(projectile->mutable_sounds()->begin() + *it);
				}

				ImGui::TreePop();
			}

			// Impact particle (asset picker instead of plain text input)
			std::string impactParticle = projectile->has_impact_particle() ? projectile->impact_particle() : "";
			static const std::set<String> impactParticleExtensions = {".hpfx"};
			if (AssetPickerWidget::Draw("Impact Particle", impactParticle, impactParticleExtensions, &m_previewManager, nullptr, 64.0f))
			{
				projectile->set_impact_particle(impactParticle);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##impact"))
			{
				ImGui::SetTooltip("Particle burst effect spawned at the impact point when the projectile hits");
			}

			ImGui::Unindent();
		}

		ImGui::Unindent();
		ImGui::PopID();
	}
}
