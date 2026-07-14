// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "emote_editor_window.h"
#include "editor_imgui_helpers.h"
#include "asset_picker_widget.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "game/emote_defs.h"
#include "game/object_type_id.h"
#include "log/default_log_levels.h"

namespace mmo
{
	namespace
	{
		const char* const s_emoteTypeNames[] = {
			"One Shot",
			"Pose",
			"Mood",
			"Pose Variant",
			"Cycle Pose"
		};
		static_assert(std::size(s_emoteTypeNames) == emote_type::Count_, "Emote type name table out of sync");

		const char* const s_poseStandStateNames[] = {
			"Stand (Special Idle)",
			"Sit",
			"Sleep",
			"Dead (unsupported)",
			"Kneel"
		};
		static_assert(std::size(s_poseStandStateNames) == unit_stand_state::Count_, "Stand state name table out of sync");
	}

	EmoteEditorWindow::EmoteEditorWindow(const String& name, proto::Project& project, EditorHost& host, PreviewProviderManager& previewManager)
		: EditorEntryWindowBase(project, project.emotes, name)
		, m_host(host)
		, m_previewManager(previewManager)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Emotes";
	}

	void EmoteEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
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

			int emoteType = static_cast<int>(currentEntry.emotetype());
			if (ImGui::Combo("Type", &emoteType, s_emoteTypeNames, static_cast<int>(std::size(s_emoteTypeNames))))
			{
				currentEntry.set_emotetype(static_cast<uint32>(emoteType));
			}
			ImGui::SameLine();
			DrawHelpMarker(
				"One Shot: plays the animation once (e.g. /wave).\n"
				"Pose: enters a persistent stand state with a looping animation (e.g. /sit).\n"
				"Mood: persistent face pose layered over the body animation (e.g. /happy).\n"
				"Pose Variant: a selectable animation for a stand-state context, cycled with /pose.\n"
				"Cycle Pose: performing it cycles the current stand state's pose variant, just like /pose.");

			std::string icon = currentEntry.has_icon() ? currentEntry.icon() : "";
			if (AssetPickerWidget::Draw("Icon", icon, asset_extensions::Textures, &m_previewManager, nullptr, 64.0f))
			{
				currentEntry.set_icon(icon);
			}
			ImGui::SameLine();
			DrawHelpMarker("Icon texture shown in the emote list and on action bar buttons. Leave empty for the generic fallback icon.");

			bool defaultKnown = (currentEntry.flags() & emote_flags::DefaultKnown) != 0;
			if (ImGui::Checkbox("Known by default", &defaultKnown))
			{
				if (defaultKnown)
				{
					currentEntry.set_flags(currentEntry.flags() | emote_flags::DefaultKnown);
				}
				else
				{
					currentEntry.set_flags(currentEntry.flags() & ~static_cast<uint32>(emote_flags::DefaultKnown));
				}
			}
			ImGui::SameLine();
			DrawHelpMarker("Every character can use this emote without unlocking it first (via quest, emote scroll, ...).");
		}

		if (const auto section = ScopedEditorSection("Animation", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Scope the widget ids: the input below is labeled "Animation" as well, which
			// would otherwise collide with the CollapsingHeader id (asserts in ButtonBehavior
			// once the text field holds the active id).
			ImGui::PushID("AnimationSection");

			std::string animation = currentEntry.has_animation() ? currentEntry.animation() : "";
			if (ImGui::InputText("Animation", &animation))
			{
				currentEntry.set_animation(animation);
			}
			ImGui::SameLine();
			DrawHelpMarker("Skeleton animation clip name to play (e.g. \"Wave\"). Meshes without this clip simply skip the animation.");

			const uint32 type = currentEntry.emotetype();
			if (type == emote_type::Pose || type == emote_type::PoseVariant)
			{
				std::string animationStart = currentEntry.has_animationstart() ? currentEntry.animationstart() : "";
				if (ImGui::InputText("Start Animation", &animationStart))
				{
					if (animationStart.empty())
					{
						currentEntry.clear_animationstart();
					}
					else
					{
						currentEntry.set_animationstart(animationStart);
					}
				}
				ImGui::SameLine();
				DrawHelpMarker("Clip played once when entering the pose (e.g. \"SleepStart\" = lay down). Empty = blend straight into the looping animation.");

				std::string animationEnd = currentEntry.has_animationend() ? currentEntry.animationend() : "";
				if (ImGui::InputText("End Animation", &animationEnd))
				{
					if (animationEnd.empty())
					{
						currentEntry.clear_animationend();
					}
					else
					{
						currentEntry.set_animationend(animationEnd);
					}
				}
				ImGui::SameLine();
				DrawHelpMarker("Clip played once on a voluntary stand-up (e.g. \"SleepEnd\"). Skipped when the pose is cancelled by movement. Empty = blend straight to idle.");

				int standState = static_cast<int>(currentEntry.standstate());
				if (ImGui::Combo("Stand State", &standState, s_poseStandStateNames, static_cast<int>(std::size(s_poseStandStateNames))))
				{
					currentEntry.set_standstate(static_cast<uint32>(standState));
				}
				ImGui::SameLine();
				DrawHelpMarker(
					"Pose: the stand state this emote enters (Sit, Sleep, Kneel).\n"
					"Pose Variant: the stand-state context this variant belongs to (Stand = special idle).");
			}

			if (type == emote_type::PoseVariant)
			{
				int variantOrder = static_cast<int>(currentEntry.variantorder());
				if (ImGui::InputInt("Variant Order", &variantOrder))
				{
					currentEntry.set_variantorder(static_cast<uint32>(std::max(0, variantOrder)));
				}
				ImGui::SameLine();
				DrawHelpMarker("Sort key used when /pose cycles through the variants of the same stand-state context.");
			}

			ImGui::PopID();
		}

		if (const auto section = ScopedEditorSection("Chat Commands", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Slash command aliases without the leading '/' (e.g. \"wave\", \"hello\").");

			int removeIndex = -1;
			for (int i = 0; i < currentEntry.aliases_size(); ++i)
			{
				ImGui::PushID(i);
				std::string alias = currentEntry.aliases(i);
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::InputText("##alias", &alias))
				{
					currentEntry.set_aliases(i, alias);
				}
				ImGui::SameLine();
				if (ImGui::Button("Remove"))
				{
					removeIndex = i;
				}
				ImGui::PopID();
			}

			if (removeIndex >= 0)
			{
				currentEntry.mutable_aliases()->DeleteSubrange(removeIndex, 1);
			}

			if (ImGui::Button("Add Alias"))
			{
				currentEntry.add_aliases("");
			}
		}

		if (const auto section = ScopedEditorSection("Chat Text", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("%%s = performing character, %%t = target name. Leave empty for a silent (animation-only) emote.");
			ImGui::TextDisabled("Translations are managed via Export / Import Translations.");

			std::string textNoTarget = currentEntry.has_textnotarget() ? currentEntry.textnotarget() : "";
			if (ImGui::InputText("No Target", &textNoTarget))
			{
				currentEntry.set_textnotarget(textNoTarget);
			}
			ImGui::SameLine();
			DrawHelpMarker("Shown when the emote is performed without a target, e.g. \"%s waves.\"");

			std::string textTarget = currentEntry.has_texttarget() ? currentEntry.texttarget() : "";
			if (ImGui::InputText("With Target", &textTarget))
			{
				currentEntry.set_texttarget(textTarget);
			}
			ImGui::SameLine();
			DrawHelpMarker("Shown when the emote targets another unit, e.g. \"%s waves at %t.\"");

			std::string textSelf = currentEntry.has_textself() ? currentEntry.textself() : "";
			if (ImGui::InputText("Self Target", &textSelf))
			{
				currentEntry.set_textself(textSelf);
			}
			ImGui::SameLine();
			DrawHelpMarker("Shown when the emote targets the performing character itself.");
		}
	}

	void EmoteEditorWindow::OnNewEntry(proto::TemplateManager<proto::Emotes, proto::EmoteEntry>::EntryType& entry)
	{
		entry.set_name("New Emote");
		entry.set_flags(0);
		entry.set_emotetype(emote_type::OneShot);
	}
}
