// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_visualization_editor_window.h"
#include "asset_picker_widget.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "editor_imgui_helpers.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "preview_providers/preview_provider_manager.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		// Editor-wide kit clipboard, shared across all entries and events.
		proto::SpellKit s_kitClipboard;
		bool s_hasKitClipboard = false;

		const String s_kitTemplateFolder = "Editor/SpellKitTemplates/";
		const String s_kitTemplateExtension = ".spellkit";

		String sanitizeKitTemplateName(const String &name)
		{
			String result;
			result.reserve(name.size());
			for (const char c : name)
			{
				if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-' || c == '(' || c == ')')
				{
					result.push_back(c);
				}
			}

			const auto begin = result.find_first_not_of(' ');
			if (begin == String::npos)
			{
				return {};
			}
			const auto end = result.find_last_not_of(' ');
			return result.substr(begin, end - begin + 1);
		}

		struct KitTemplateInfo
		{
			String name;
			String path;
		};

		std::vector<KitTemplateInfo> listKitTemplates()
		{
			std::vector<KitTemplateInfo> templates;
			for (const std::string &file : AssetRegistry::ListFiles(s_kitTemplateFolder, s_kitTemplateExtension))
			{
				KitTemplateInfo info;
				info.path = file;

				String name = file;
				if (name.starts_with(s_kitTemplateFolder))
				{
					name = name.substr(s_kitTemplateFolder.size());
				}
				if (name.ends_with(s_kitTemplateExtension))
				{
					name = name.substr(0, name.size() - s_kitTemplateExtension.size());
				}
				info.name = name;

				templates.push_back(std::move(info));
			}

			std::sort(templates.begin(), templates.end(), [](const KitTemplateInfo &a, const KitTemplateInfo &b)
			{
				return a.name < b.name;
			});
			return templates;
		}

		bool saveKitTemplate(const String &name, const proto::SpellKit &kit)
		{
			const String fileName = sanitizeKitTemplateName(name);
			if (fileName.empty())
			{
				ELOG("Cannot save spell kit template: invalid name '" << name << "'");
				return false;
			}

			const String path = s_kitTemplateFolder + fileName + s_kitTemplateExtension;
			const auto file = AssetRegistry::CreateNewFile(path);
			if (!file)
			{
				ELOG("Failed to create spell kit template file " << path);
				return false;
			}

			if (!kit.SerializeToOstream(file.get()))
			{
				ELOG("Failed to serialize spell kit template to " << path);
				return false;
			}

			file->flush();
			ILOG("Saved spell kit template to " << path);
			return true;
		}

		bool loadKitTemplate(const String &path, proto::SpellKit &out)
		{
			const auto file = AssetRegistry::OpenFile(path);
			if (!file)
			{
				ELOG("Failed to open spell kit template file " << path);
				return false;
			}

			if (!out.ParseFromIstream(file.get()))
			{
				ELOG("Failed to parse spell kit template from " << path);
				return false;
			}

			return true;
		}

		void createStarterKitTemplates()
		{
			// Warm glow on the casting hand while channeling.
			{
				proto::SpellKit kit;
				kit.set_scope(proto::CASTER);
				kit.set_attach_bone("hand_right");
				kit.set_loop(true);
				auto *light = kit.mutable_light();
				light->set_r(1.0f);
				light->set_g(0.85f);
				light->set_b(0.55f);
				light->set_intensity(2.0f);
				light->set_range(5.0f);
				light->set_fade_in_time(0.2f);
				light->set_fade_out_time(0.3f);
				saveKitTemplate("Hand Glow (Cast Loop)", kit);
			}

			// Short bright flash on the target at impact.
			{
				proto::SpellKit kit;
				kit.set_scope(proto::TARGET);
				kit.set_duration_ms(500);
				auto *light = kit.mutable_light();
				light->set_r(1.0f);
				light->set_g(0.7f);
				light->set_b(0.3f);
				light->set_intensity(3.0f);
				light->set_range(8.0f);
				light->set_fade_in_time(0.05f);
				light->set_fade_out_time(0.4f);
				saveKitTemplate("Impact Flash", kit);
			}

			// Soft looping shimmer while an aura is active.
			{
				proto::SpellKit kit;
				kit.set_scope(proto::TARGET);
				kit.set_loop(true);
				auto *tint = kit.mutable_tint();
				tint->set_r(0.4f);
				tint->set_g(0.6f);
				tint->set_b(1.0f);
				tint->set_a(0.5f);
				auto *light = kit.mutable_light();
				light->set_r(0.4f);
				light->set_g(0.6f);
				light->set_b(1.0f);
				light->set_intensity(1.0f);
				light->set_range(4.0f);
				light->set_fade_in_time(0.4f);
				light->set_fade_out_time(0.6f);
				saveKitTemplate("Aura Shimmer", kit);
			}

			// Gentle green glow on the healed target.
			{
				proto::SpellKit kit;
				kit.set_scope(proto::TARGET);
				kit.set_duration_ms(1000);
				auto *light = kit.mutable_light();
				light->set_r(0.5f);
				light->set_g(1.0f);
				light->set_b(0.6f);
				light->set_intensity(2.0f);
				light->set_range(6.0f);
				light->set_fade_in_time(0.15f);
				light->set_fade_out_time(0.5f);
				saveKitTemplate("Heal Glow", kit);
			}

			ILOG("Created starter spell kit templates in " << s_kitTemplateFolder);
		}
	}

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
				if (ImGui::Button("Duplicate Selected", ImVec2(-1, 0)))
				{
					DuplicateSelectedEntry();
				}
				ImGui::EndDisabled();

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

			DrawKitTemplateNameDialog();
		}
		ImGui::End();

		return false;
	}

	void SpellVisualizationEditorWindow::DrawKitTemplateNameDialog()
	{
		if (m_showKitTemplateDialog)
		{
			ImGui::OpenPopup("Save Kit Template");
			m_showKitTemplateDialog = false;
		}

		if (ImGui::BeginPopupModal("Save Kit Template", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter a name for the kit template:");
			ImGui::InputText("##kitTemplateName", &m_kitTemplateName);

			const String sanitized = sanitizeKitTemplateName(m_kitTemplateName);
			const bool nameValid = !sanitized.empty();
			const bool exists = nameValid && AssetRegistry::HasFile(s_kitTemplateFolder + sanitized + s_kitTemplateExtension);
			if (exists)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "A template with this name already exists and will be overwritten.");
			}

			ImGui::BeginDisabled(!nameValid);
			if (ImGui::Button(exists ? "Overwrite" : "Save"))
			{
				saveKitTemplate(sanitized, m_kitTemplateSource);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
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

		// Spell-driven preview: pick a spell that references this visualization to
		// drive cast time and projectile speed from real spell data.
		if (currentEntry)
		{
			// Collect all spells referencing this visualization
			std::vector<const proto::SpellEntry*> linkedSpells;
			for (const auto& spell : m_project.spells.getTemplates().entry())
			{
				if (spell.has_visualization_id() && spell.visualization_id() == currentEntry->id())
				{
					linkedSpells.push_back(&spell);
				}
			}

			if (linkedSpells.empty())
			{
				ImGui::TextDisabled("Preview as Spell: no spells reference this visualization yet.");
				m_previewSpellId = 0;
			}
			else
			{
				// Resolve the current selection (may have been unlinked meanwhile)
				const proto::SpellEntry* selectedSpell = nullptr;
				for (const auto* spell : linkedSpells)
				{
					if (spell->id() == m_previewSpellId)
					{
						selectedSpell = spell;
						break;
					}
				}
				if (!selectedSpell)
				{
					m_previewSpellId = 0;
				}

				ImGui::SetNextItemWidth(220.0f);
				const String comboLabel = selectedSpell
					? std::to_string(selectedSpell->id()) + ": " + selectedSpell->name()
					: "Manual (use preview settings)";
				if (ImGui::BeginCombo("Preview as Spell", comboLabel.c_str()))
				{
					if (ImGui::Selectable("Manual (use preview settings)", m_previewSpellId == 0))
					{
						m_previewSpellId = 0;
					}
					for (const auto* spell : linkedSpells)
					{
						char spellLabel[256];
						snprintf(spellLabel, sizeof(spellLabel), "%u: %s", spell->id(), spell->name().c_str());
						if (ImGui::Selectable(spellLabel, spell->id() == m_previewSpellId))
						{
							m_previewSpellId = spell->id();
						}
					}
					ImGui::EndCombo();
				}

				// While a spell drives the preview, keep cast time and projectile speed in sync
				// with the spell data so edits in the spell editor are picked up immediately.
				if (selectedSpell)
				{
					const float castSeconds = selectedSpell->casttime() / 1000.0f;
					m_preview->SetCastDuration(castSeconds);
					m_preview->SetProjectileSpeed(selectedSpell->speed() > 0.0f ? selectedSpell->speed() : m_previewProjectileSpeed);

					ImGui::SameLine();
					if (castSeconds <= 0.0f)
					{
						ImGui::TextDisabled("(instant, speed %.1f)", selectedSpell->speed());
					}
					else
					{
						ImGui::TextDisabled("(%.1fs cast, speed %.1f)", castSeconds, selectedSpell->speed());
					}
				}
			}
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
		static const auto& iconExtensions = asset_extensions::Textures;
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

				// Adjust tint
				auto* tint = newKit->mutable_tint();
				if (tint)
				{
					tint->set_r(0.0f);
					tint->set_g(0.0f);
					tint->set_b(0.0f);
					tint->set_a(0.0f);
				}
			}

			ImGui::SameLine();
			ImGui::BeginDisabled(!s_hasKitClipboard);
			if (ImGui::Button("Paste Kit"))
			{
				kitsMap[eventValue].add_kits()->CopyFrom(s_kitClipboard);
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button("From Template"))
			{
				ImGui::OpenPopup("KitTemplatePopup");
			}
			if (ImGui::BeginPopup("KitTemplatePopup"))
			{
				const auto templates = listKitTemplates();
				if (templates.empty())
				{
					ImGui::TextDisabled("(no templates yet)");
					if (ImGui::MenuItem("Create Starter Templates"))
					{
						createStarterKitTemplates();
					}
				}
				for (const auto &tpl : templates)
				{
					if (ImGui::MenuItem(tpl.name.c_str()))
					{
						proto::SpellKit kit;
						if (loadKitTemplate(tpl.path, kit))
						{
							kitsMap[eventValue].add_kits()->CopyFrom(kit);
						}
					}
				}
				ImGui::EndPopup();
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

			// Show the actual animation length so the duration doesn't have to be guessed
			if (m_preview && !animName.empty())
			{
				const float animSeconds = m_preview->GetCasterAnimationDuration(animName);
				if (animSeconds >= 0.0f)
				{
					const int animMs = static_cast<int>(animSeconds * 1000.0f);
					ImGui::SameLine();
					ImGui::TextDisabled("anim: %d ms", animMs);
					ImGui::SameLine();
					if (ImGui::SmallButton("Set from anim"))
					{
						kit.set_duration_ms(animMs);
					}
				}
				else
				{
					ImGui::SameLine();
					ImGui::TextDisabled("(anim not found on caster)");
				}
			}

			// Delay
			int delay = kit.has_delay_ms() ? kit.delay_ms() : 0;
			if (ImGui::InputInt("Delay (ms)", &delay))
			{
				kit.set_delay_ms(std::max(0, delay));
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##delay"))
			{
				ImGui::SetTooltip("Delay before this kit fires after its event was triggered.\nUse to stagger multiple kits within the same event (e.g., flash, then smoke).");
			}

			// --- Sounds ---
			ImGui::Spacing();
			if (ImGui::TreeNode("Sounds"))
			{
				if (ImGui::Button("Add Sound"))
				{
					kit.add_sounds("Sound/Spells/NewSound.wav");
				}

				static const auto& soundExtensions = asset_extensions::Sounds;
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

				static const auto& particleExtensions = asset_extensions::Particles;
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
				static const auto& meshExtensions = asset_extensions::Meshes;
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
					static const auto& materialExtensions = asset_extensions::BaseMaterials;
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

			// Kit actions
			ImGui::Spacing();
			if (ImGui::Button("Copy Kit"))
			{
				s_kitClipboard.CopyFrom(kit);
				s_hasKitClipboard = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Save as Template..."))
			{
				m_kitTemplateSource.CopyFrom(kit);
				m_kitTemplateName.clear();
				m_showKitTemplateDialog = true;
			}
			ImGui::SameLine();
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

	/// @brief Draw the config UI for a single ProjectileVisual entry.
	void SpellVisualizationEditorWindow::DrawSingleProjectileConfig(proto::ProjectileVisual &projectile, int index)
	{
		ImGui::PushID(index);

		// Spawn bone
		std::string spawnBone = projectile.has_spawn_bone() ? projectile.spawn_bone() : "";
		if (ImGui::InputText("Spawn Bone", &spawnBone))
		{
			projectile.set_spawn_bone(spawnBone);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("?##spawnbone"))
		{
			ImGui::SetTooltip("Bone name to spawn the projectile from (e.g., hand_right).\nLeave empty to use a default offset above the caster.");
		}

		// Spawn offsets
		float offsetRight = projectile.has_spawn_offset_right() ? projectile.spawn_offset_right() : 0.0f;
		if (ImGui::DragFloat("Spawn Offset Right", &offsetRight, 0.05f, -10.0f, 10.0f, "%.2f"))
		{
			projectile.set_spawn_offset_right(offsetRight);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("?##offsetright"))
		{
			ImGui::SetTooltip("Horizontal offset to the right of the cast direction.\nNegative values offset to the left.\nUsed to spread multiple projectiles side by side.");
		}

		float offsetUp = projectile.has_spawn_offset_up() ? projectile.spawn_offset_up() : 0.0f;
		if (ImGui::DragFloat("Spawn Offset Up", &offsetUp, 0.05f, -10.0f, 10.0f, "%.2f"))
		{
			projectile.set_spawn_offset_up(offsetUp);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("?##offsetup"))
		{
			ImGui::SetTooltip("Vertical offset from the spawn position.\nNegative values offset downward.");
		}

		ImGui::Separator();

		// Movement section
		if (ImGui::CollapsingHeader("Movement", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			// Motion type
			int motionType = projectile.has_motion() ? static_cast<int>(projectile.motion()) : 0;
			if (ImGui::Combo("Motion Type", &motionType, s_motionTypeNames, IM_ARRAYSIZE(s_motionTypeNames)))
			{
				projectile.set_motion(static_cast<proto::ProjectileMotion>(motionType));
			}

			// Motion-specific parameters
			switch (projectile.motion())
			{
			case proto::ARC:
				{
					float arcHeight = projectile.has_arc_height() ? projectile.arc_height() : 0.0f;
					if (ImGui::DragFloat("Arc Height", &arcHeight, 0.1f, -50.0f, 50.0f))
					{
						projectile.set_arc_height(arcHeight);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("?##archeight"))
					{
						ImGui::SetTooltip("Vertical offset of the parabolic arc in world units.\nPositive = up, Negative = down.");
					}

					float arcWidth = projectile.has_arc_width() ? projectile.arc_width() : 0.0f;
					if (ImGui::DragFloat("Arc Width", &arcWidth, 0.1f, -50.0f, 50.0f))
					{
						projectile.set_arc_width(arcWidth);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("?##arcwidth"))
					{
						ImGui::SetTooltip("Horizontal offset of the parabolic arc in world units.\nPerpendicular to the travel direction.\nCombine with Arc Height for diagonal arcs.");
					}
					break;
				}
			case proto::HOMING:
				{
					float homingStrength = projectile.has_homing_strength() ? projectile.homing_strength() : 5.0f;
					if (ImGui::DragFloat("Homing Strength", &homingStrength, 0.1f, 0.1f, 20.0f))
					{
						projectile.set_homing_strength(homingStrength);
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
					float frequency = projectile.has_wave_frequency() ? projectile.wave_frequency() : 1.0f;
					if (ImGui::DragFloat("Wave Frequency", &frequency, 0.1f, 0.1f, 10.0f))
					{
						projectile.set_wave_frequency(frequency);
					}

					float amplitude = projectile.has_wave_amplitude() ? projectile.wave_amplitude() : 1.0f;
					if (ImGui::DragFloat("Wave Amplitude", &amplitude, 0.1f, 0.0f, 10.0f))
					{
						projectile.set_wave_amplitude(amplitude);
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
			std::string meshName = projectile.has_mesh_name() ? projectile.mesh_name() : "";
			static const auto& meshExtensions = asset_extensions::Meshes;
			if (AssetPickerWidget::Draw("Mesh", meshName, meshExtensions, &m_previewManager, nullptr, 64.0f))
			{
				projectile.set_mesh_name(meshName);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##mesh"))
			{
				ImGui::SetTooltip("3D mesh for the projectile (e.g., arrow, fireball orb)");
			}

			// Material name
			std::string materialName = projectile.has_material_name() ? projectile.material_name() : "";
			static const auto& materialExtensions = asset_extensions::BaseMaterials;
			if (AssetPickerWidget::Draw("Material", materialName, materialExtensions, nullptr, nullptr, 0.0f))
			{
				projectile.set_material_name(materialName);
			}

			// Trail particle
			std::string trailParticle = projectile.has_trail_particle() ? projectile.trail_particle() : "";
			static const auto& particleExtensions = asset_extensions::Particles;
			if (AssetPickerWidget::Draw("Trail Particle", trailParticle, particleExtensions, &m_previewManager, nullptr, 64.0f))
			{
				projectile.set_trail_particle(trailParticle);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##trail"))
			{
				ImGui::SetTooltip("Particle system for trailing effect behind the projectile");
			}

			// Scale
			float scale = projectile.has_scale() ? projectile.scale() : 1.0f;
			if (ImGui::DragFloat("Scale", &scale, 0.01f, 0.1f, 10.0f))
			{
				projectile.set_scale(scale);
			}

			ImGui::Unindent();
		}

		// Rotation section
		if (ImGui::CollapsingHeader("Rotation"))
		{
			ImGui::Indent();

			// Face movement
			bool faceMovement = projectile.has_face_movement() ? projectile.face_movement() : true;
			if (ImGui::Checkbox("Face Movement Direction", &faceMovement))
			{
				projectile.set_face_movement(faceMovement);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##face"))
			{
				ImGui::SetTooltip("Automatically orient projectile along velocity vector");
			}

			// Spin rate
			float spinRate = projectile.has_spin_rate() ? projectile.spin_rate() : 0.0f;
			if (ImGui::DragFloat("Spin Rate (deg/sec)", &spinRate, 1.0f, -720.0f, 720.0f))
			{
				projectile.set_spin_rate(spinRate);
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

			bool hasProjLight = projectile.has_light();
			if (ImGui::Checkbox("Enable Projectile Light", &hasProjLight))
			{
				if (hasProjLight)
				{
					auto* light = projectile.mutable_light();
					light->set_r(1.0f);
					light->set_g(0.6f);
					light->set_b(0.2f);
					light->set_intensity(2.0f);
					light->set_range(8.0f);
				}
				else
				{
					projectile.clear_light();
				}
			}

			if (projectile.has_light())
			{
				auto* light = projectile.mutable_light();

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

			bool hasProjRibbon = projectile.has_ribbon_trail();
			if (ImGui::Checkbox("Enable Projectile Ribbon Trail", &hasProjRibbon))
			{
				if (hasProjRibbon)
				{
					auto* ribbon = projectile.mutable_ribbon_trail();
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
					projectile.clear_ribbon_trail();
				}
			}

			if (projectile.has_ribbon_trail())
			{
				auto* ribbon = projectile.mutable_ribbon_trail();

				// Material
				std::string ribbonMaterial = ribbon->has_material_name() ? ribbon->material_name() : "";
				static const auto& ribbonMatExtensions = asset_extensions::BaseMaterials;
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
					projectile.add_sounds("Sound/Spells/Projectile.wav");
				}

				static const auto& soundExtensions = asset_extensions::Sounds;
				std::vector<int> soundsToRemove;

				for (int i = 0; i < projectile.sounds_size(); ++i)
				{
					ImGui::PushID(i);

					std::string sound = projectile.sounds(i);
					char soundLabel[32];
					snprintf(soundLabel, sizeof(soundLabel), "Sound %d", i);

					if (AssetPickerWidget::Draw(soundLabel, sound, soundExtensions, nullptr, m_audioSystem, 0.0f))
					{
						projectile.set_sounds(i, sound);
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
					projectile.mutable_sounds()->erase(projectile.mutable_sounds()->begin() + *it);
				}

				ImGui::TreePop();
			}

			// Impact particle (asset picker instead of plain text input)
			std::string impactParticle = projectile.has_impact_particle() ? projectile.impact_particle() : "";
			static const auto& impactParticleExtensions = asset_extensions::Particles;
			if (AssetPickerWidget::Draw("Impact Particle", impactParticle, impactParticleExtensions, &m_previewManager, nullptr, 64.0f))
			{
				projectile.set_impact_particle(impactParticle);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("?##impact"))
			{
				ImGui::SetTooltip("Particle burst effect spawned at the impact point when the projectile hits");
			}

			ImGui::Unindent();
		}

		ImGui::PopID();
	}

	void SpellVisualizationEditorWindow::DrawProjectileConfig(proto::SpellVisualization &currentEntry)
	{
		ImGui::PushID("ProjectileConfig");

		const int projectileCount = currentEntry.projectiles_size();
		const bool hasLegacyProjectile = currentEntry.has_projectile();
		const bool hasAnyProjectile = projectileCount > 0 || hasLegacyProjectile;

		// Preview speed setting (shared, affects editor preview only)
		ImGui::TextDisabled("Preview Settings (Editor Only)");
		if (ImGui::DragFloat("Preview Speed", &m_previewProjectileSpeed, 0.5f, 1.0f, 100.0f, "%.1f units/sec"))
		{
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

		// Migration: if legacy single projectile exists and no repeated entries, offer migration
		if (hasLegacyProjectile && projectileCount == 0)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Legacy single projectile detected.");
			if (ImGui::Button("Migrate to Multi-Projectile"))
			{
				// Copy the legacy projectile into the repeated list
				*currentEntry.add_projectiles() = currentEntry.projectile();
				currentEntry.clear_projectile();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Moves the existing projectile config into the new\nmulti-projectile list. After migration, you can add more projectiles.");
			}

			ImGui::Separator();

			// Still show the legacy single-projectile editor
			ImGui::Text("Projectile (Legacy)");
			ImGui::Indent();
			DrawSingleProjectileConfig(*currentEntry.mutable_projectile(), 0);
			ImGui::Unindent();
		}
		else
		{
			// Multi-projectile UI
			ImGui::Text("Projectiles: %d", projectileCount);
			ImGui::SameLine();

			if (ImGui::Button("Add Projectile"))
			{
				auto* proj = currentEntry.add_projectiles();
				proj->set_motion(proto::LINEAR);
				proj->set_scale(1.0f);
				proj->set_face_movement(true);
			}

			int removeIndex = -1;
			int duplicateIndex = -1;

			for (int i = 0; i < projectileCount; ++i)
			{
				ImGui::PushID(i);

				char headerLabel[64];
				snprintf(headerLabel, sizeof(headerLabel), "Projectile %d", i + 1);

				bool opened = ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_DefaultOpen);

				// Buttons on the same line as the header
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100.0f);
				if (ImGui::SmallButton("Duplicate"))
				{
					duplicateIndex = i;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					removeIndex = i;
				}

				if (opened)
				{
					ImGui::Indent();
					DrawSingleProjectileConfig(*currentEntry.mutable_projectiles(i), i);
					ImGui::Unindent();
				}

				ImGui::PopID();
			}

			// Handle duplicate
			if (duplicateIndex >= 0 && duplicateIndex < projectileCount)
			{
				*currentEntry.add_projectiles() = currentEntry.projectiles(duplicateIndex);
			}

			// Handle removal
			if (removeIndex >= 0 && removeIndex < currentEntry.projectiles_size())
			{
				currentEntry.mutable_projectiles()->erase(
					currentEntry.mutable_projectiles()->begin() + removeIndex);
			}
		}

		ImGui::PopID();
	}
}
