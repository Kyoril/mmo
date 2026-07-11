// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "race_editor_window.h"
#include "editor_imgui_helpers.h"

#include "faction_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "game/spell.h"
#include "log/default_log_levels.h"
#include "math/constants.h"

namespace mmo
{
	RaceEditorWindow::RaceEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.races, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Races";
	}

	void RaceEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_BOOL_PROP(name, label) \
	{ \
		bool value = currentEntry.name(); \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_FLAG_PROP(property, label, flags) \
	{ \
		bool value = (currentEntry.property() & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.set_##property(currentEntry.property() | static_cast<uint32>(flags)); \
			else \
				currentEntry.set_##property(currentEntry.property() & ~static_cast<uint32>(flags)); \
		} \
	}

#define VECTOR3_PROP(xname, yname, zname, label) \
	{ \
		float values[3] = { currentEntry.xname(), currentEntry.yname(), currentEntry.zname() }; \
		if (ImGui::InputFloat3(label, values, "%.3f", ImGuiInputTextFlags_None)) \
		{ \
			currentEntry.set_##xname(values[0]); \
			currentEntry.set_##yname(values[1]); \
			currentEntry.set_##zname(values[2]); \
		} \
	}

#define SLIDER_FLOAT_PROP(name, label, min, max) \
	{ \
		const char* format = "%.2f"; \
		float value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_Float, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}

#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

		// Quick setup state to streamline race creation flow.
		// Validate by existence in project data, because id "0" can be valid in some projects.
		const bool hasFactionTemplate = m_project.factionTemplates.getById(currentEntry.factiontemplate()) != nullptr;
		const bool hasStartMap = m_project.maps.getById(currentEntry.startmap()) != nullptr;
		const bool hasMaleModel = m_project.models.getById(currentEntry.malemodel()) != nullptr;
		const bool hasFemaleModel = m_project.models.getById(currentEntry.femalemodel()) != nullptr;
		const bool isSetupComplete = hasFactionTemplate && hasStartMap && hasMaleModel && hasFemaleModel;
		if (ImGui::BeginTable("raceSetupStatus", 4, ImGuiTableFlags_SizingStretchProp))
		{
			auto drawStatus = [](const char* label, bool ready)
			{
				ImGui::TableNextColumn();
				ImGui::PushStyleColor(ImGuiCol_Text, ready ? ImVec4(0.3f, 0.8f, 0.4f, 1.0f) : ImVec4(0.9f, 0.65f, 0.2f, 1.0f));
				ImGui::Text("%s: %s", label, ready ? "OK" : "Missing");
				ImGui::PopStyleColor();
			};

			drawStatus("Faction", hasFactionTemplate);
			drawStatus("Start Map", hasStartMap);
			drawStatus("Male Model", hasMaleModel);
			drawStatus("Female Model", hasFemaleModel);
			ImGui::EndTable();
		}
		if (!isSetupComplete)
		{
			ImGui::TextDisabled("Fix missing entries in: Basic -> Faction Template, Starting point -> Map, Visuals -> Male/Female Model.");
		}
		ImGui::Spacing();

		static const char* s_factionTemplateNone = "<None>";

		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawSectionHeader("Identity");
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

			DrawSectionHeader("Availability");
			CHECKBOX_BOOL_PROP(disabled, "Disabled (race locked on this realm)");
			ImGui::Spacing();

			DrawSectionHeader("Gameplay Defaults");
			int32 factionTemplate = currentEntry.factiontemplate();

			const auto* factionEntry = m_project.factionTemplates.getById(factionTemplate);
			if (ImGui::BeginCombo("Faction Template", factionEntry != nullptr ? factionEntry->name().c_str() : s_factionTemplateNone, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.factionTemplates.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.factionTemplates.getTemplates().entry(i).id() == factionTemplate;
					const char* item_text = m_project.factionTemplates.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_factiontemplate(m_project.factionTemplates.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}
		}

		static const char* s_mapEntryNone = "<None>";

		if (const auto section = ScopedEditorSection("Starting point", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawSectionHeader("Spawn Location");
			int32 startMap = currentEntry.startmap();

			const auto* mapEntry = m_project.maps.getById(startMap);
			if (ImGui::BeginCombo("Map", mapEntry != nullptr ? mapEntry->name().c_str() : s_mapEntryNone, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.maps.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.maps.getTemplates().entry(i).id() == startMap;
					const char* item_text = m_project.maps.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_startmap(m_project.maps.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			VECTOR3_PROP(startposx, startposy, startposz, "Starting Position");
			SLIDER_FLOAT_PROP(startrotation, "Starting Rotation", 0.0f, Pi * 2.0f);
		}


		if (const auto section = ScopedEditorSection("Visuals", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawSectionHeader("Character Models");
			int32 maleModel = currentEntry.malemodel();

			const auto* maleModelEntry = m_project.models.getById(maleModel);
			if (ImGui::BeginCombo("Male Model", maleModelEntry != nullptr ? maleModelEntry->name().c_str() : s_factionTemplateNone, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.models.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.models.getTemplates().entry(i).id() == maleModel;
					const char* item_text = m_project.models.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_malemodel(m_project.models.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			int32 femaleModel = currentEntry.femalemodel();

			const auto* femaleModelEntry = m_project.models.getById(femaleModel);
			if (ImGui::BeginCombo("Female Model", femaleModelEntry != nullptr ? femaleModelEntry->name().c_str() : s_factionTemplateNone, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.models.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.models.getTemplates().entry(i).id() == femaleModel;
					const char* item_text = m_project.models.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_femalemodel(m_project.models.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}
		}

		if (const auto section = ScopedEditorSection("Initial Items", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static const char* s_itemNone = "<None>";
			DrawSectionHeader("Starter Gear Per Class");

			if (ImGui::BeginTable("initialItemsTable", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("Class & Items", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				// Iterate through all available classes
				for (int classIndex = 0; classIndex < m_project.classes.count(); classIndex++)
				{
					const auto& classEntry = m_project.classes.getTemplates().entry(classIndex);
					uint32 classId = classEntry.id();

					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					// Create a unique ID for this class section
					ImGui::PushID(classId);

					// Class header with expand/collapse
					bool classNodeOpen = ImGui::TreeNodeEx(classEntry.name().c_str(), ImGuiTreeNodeFlags_DefaultOpen);
					
					if (classNodeOpen)
					{
						// Get or create initial items for this class
						auto* initialItems = currentEntry.mutable_initialitems();
						auto itemsIt = initialItems->find(classId);
						if (itemsIt == initialItems->end())
						{
							// Create new entry for this class
							(*initialItems)[classId] = proto::InitialItems();
							itemsIt = initialItems->find(classId);
						}

						auto& classInitialItems = itemsIt->second;

						// Display current items for this class
						auto& itemsList = *classInitialItems.mutable_items();
						
						// Show existing items
						for (int itemIndex = 0; itemIndex < itemsList.size(); itemIndex++)
						{
							ImGui::PushID(itemIndex);
							
							uint32 itemId = itemsList[itemIndex];
							const auto* itemEntry = m_project.items.getById(itemId);
							
							ImGui::Indent();
							
							// Item combo box
							if (ImGui::BeginCombo("##item", itemEntry != nullptr ? itemEntry->name().c_str() : s_itemNone, ImGuiComboFlags_None))
							{
								// Allow selecting "None" to remove item
								if (ImGui::Selectable(s_itemNone, itemId == 0))
								{
									itemsList.erase(itemsList.begin() + itemIndex);
									ImGui::EndCombo();
									ImGui::Unindent();
									ImGui::PopID();
									break; // Break to avoid iterator invalidation
								}

								for (int i = 0; i < m_project.items.count(); i++)
								{
									ImGui::PushID(i);
									const bool item_selected = m_project.items.getTemplates().entry(i).id() == itemId;
									const char* item_text = m_project.items.getTemplates().entry(i).name().c_str();
									if (ImGui::Selectable(item_text, item_selected))
									{
										itemsList[itemIndex] = m_project.items.getTemplates().entry(i).id();
									}
									if (item_selected)
									{
										ImGui::SetItemDefaultFocus();
									}
									ImGui::PopID();
								}
								ImGui::EndCombo();
							}

							// Remove button
							ImGui::SameLine();
							if (DrawDangerButton("Remove"))
							{
								itemsList.erase(itemsList.begin() + itemIndex);
								ImGui::Unindent();
								ImGui::PopID();
								break; // Break to avoid iterator invalidation
							}
							
							ImGui::Unindent();
							ImGui::PopID();
						}

						// Add new item button
						ImGui::Indent();
						if (DrawSuccessButton("Add Item"))
						{
							itemsList.Add(0); // Add a placeholder item that can be configured
						}
						ImGui::Unindent();

						ImGui::TreePop();
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Allowed Classes", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawSectionHeader("Class Availability");
			ImGui::TextDisabled("Classes this race may be (character creation and class change). If none are checked, legality falls back to whichever classes have initial items / spells / action buttons defined above.");
			ImGui::Spacing();

			auto* allowedClasses = currentEntry.mutable_allowedclasses();
			for (int classIndex = 0; classIndex < m_project.classes.count(); ++classIndex)
			{
				const auto& classEntry = m_project.classes.getTemplates().entry(classIndex);
				const uint32 classId = classEntry.id();

				// Locate this class in the repeated allowed-classes field.
				int foundIndex = -1;
				for (int i = 0; i < allowedClasses->size(); ++i)
				{
					if (allowedClasses->Get(i) == classId)
					{
						foundIndex = i;
						break;
					}
				}

				ImGui::PushID(static_cast<int>(classId));
				bool checked = (foundIndex != -1);
				if (ImGui::Checkbox(classEntry.name().c_str(), &checked))
				{
					if (checked && foundIndex == -1)
					{
						allowedClasses->Add(classId);
					}
					else if (!checked && foundIndex != -1)
					{
						// Order is irrelevant: swap the entry to the end and drop the last element.
						allowedClasses->SwapElements(foundIndex, allowedClasses->size() - 1);
						allowedClasses->RemoveLast();
					}
				}
				ImGui::PopID();
			}
		}

		if (const auto section = ScopedEditorSection("Voice Lines", ImGuiTreeNodeFlags_None))
		{
			DrawSectionHeader("Cast Error Voice Lines");
			ImGui::TextDisabled("Voice lines (SoundEntry references) played when a spell cast of a character of this race fails. Unset entries stay silent.");
			ImGui::Spacing();

			struct CastErrorVoiceLine
			{
				uint32 castResult;
				const char* label;
			};

			struct NoPowerVoiceLine
			{
				uint32 powerType;
				const char* label;
			};

			// Values must match spell_cast_result in game/spell.h.
			static const CastErrorVoiceLine s_castErrorVoiceLines[] = {
				{ spell_cast_result::FailedOutOfRange, "Out of Range" },
				{ spell_cast_result::FailedNoPower, "Not Enough Power (Generic Fallback)" },
				{ spell_cast_result::FailedNotReady, "Not Ready (Cooldown)" },
			};

			// Values must match power_type in game/spell.h.
			static const NoPowerVoiceLine s_noPowerVoiceLines[] = {
				{ power_type::Mana, "No Mana" },
				{ power_type::Rage, "No Rage" },
				{ power_type::Energy, "No Energy" },
			};

			const auto drawGenderVoiceLines = [this](const char* genderLabel, proto::VoiceLineSet* voiceSet)
			{
				ImGui::PushID(genderLabel);
				ImGui::Separator();
				ImGui::TextUnformatted(genderLabel);

				// Draws one voice line combo backed by one key of a proto uint32->uint32
				// map; picking "(None)" erases the key so unset entries stay absent.
				const auto drawVoiceLineCombo = [this](const char* label, auto* soundsByKey, const uint32 key)
				{
					ImGui::PushID(label);

					uint32 currentSoundId = 0;
					if (const auto it = soundsByKey->find(key); it != soundsByKey->end())
					{
						currentSoundId = it->second;
					}

					const proto::SoundEntry* currentSound = (currentSoundId != 0) ? m_project.sounds.getById(currentSoundId) : nullptr;
					if (ImGui::BeginCombo(label, currentSound ? currentSound->name().c_str() : "(None)", ImGuiComboFlags_HeightLargest))
					{
						if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
						{
							ImGui::SetKeyboardFocusHere(0);
						}

						m_voiceSoundFilter.Draw("##voice_sound_filter", -1.0f);

						if (ImGui::Selectable("(None)"))
						{
							soundsByKey->erase(key);
							m_voiceSoundFilter.Clear();
							ImGui::CloseCurrentPopup();
						}

						if (ImGui::BeginChild("##voice_sound_scroll_area", ImVec2(0, 400)))
						{
							for (const auto& sound : m_project.sounds.getTemplates().entry())
							{
								if (m_voiceSoundFilter.IsActive() && !m_voiceSoundFilter.PassFilter(sound.name().c_str()))
								{
									continue;
								}

								ImGui::PushID(sound.id());
								if (ImGui::Selectable(sound.name().c_str()))
								{
									(*soundsByKey)[key] = sound.id();

									m_voiceSoundFilter.Clear();
									ImGui::CloseCurrentPopup();
								}
								ImGui::PopID();
							}
						}
						ImGui::EndChild();

						ImGui::EndCombo();
					}

					ImGui::PopID();
				};

				for (const auto& voiceLine : s_castErrorVoiceLines)
				{
					drawVoiceLineCombo(voiceLine.label, voiceSet->mutable_cast_error_sounds(), voiceLine.castResult);
				}

				ImGui::Spacing();
				ImGui::TextDisabled("Power type specific no-power lines take precedence over the generic fallback above.");
				for (const auto& voiceLine : s_noPowerVoiceLines)
				{
					drawVoiceLineCombo(voiceLine.label, voiceSet->mutable_no_power_sounds(), voiceLine.powerType);
				}

				ImGui::PopID();
			};

			drawGenderVoiceLines("Male", currentEntry.mutable_male_voice());
			drawGenderVoiceLines("Female", currentEntry.mutable_female_voice());
		}
	}

	void RaceEditorWindow::OnNewEntry(proto::TemplateManager<proto::Races, proto::RaceEntry>::EntryType& entry)
	{
		entry.set_factiontemplate(0);
		entry.set_malemodel(0);
		entry.set_femalemodel(0);
		entry.set_baselanguage(0);
		entry.set_startingtaximask(0);
		entry.set_startmap(0);
		entry.set_startzone(0);
		entry.set_startposx(0.0f);
		entry.set_startposy(0.0f);
		entry.set_startposz(0.0f);
		entry.set_startrotation(0.0f);
		entry.set_cinematic(0);
	}
}
