// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "animation_profile_editor_window.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "shared/proto_data/animation_profiles.pb.h"

namespace mmo
{
	namespace
	{
		const char* const s_animationSlotNames[] = {
			"Idle",
			"Combat Idle",
			"Move Forward",
			"Move Backward",
			"Move Left",
			"Move Right",
			"Move Forward Left",
			"Move Forward Right",
			"Move Backward Left",
			"Move Backward Right",
			"Jump Start",
			"Fall",
			"Land",
			"Death",
			"Hit",
			"Attack",
			"Special Idle",
			"Loot"
		};
		static_assert(std::size(s_animationSlotNames) == proto::ANIM_SLOT_LOOT + 1,
			"Animation slot name table out of sync");

		const char* const s_animationConditionNames[] = {
			"Walk",
			"Swim",
			"Stealth",
			"Combat",
			"Combat (Unarmed)",
			"Combat (1H Weapon)",
			"Combat (2H Weapon)"
		};
		static_assert(std::size(s_animationConditionNames) == proto::ANIM_COND_COMBAT_2H + 1,
			"Animation condition name table out of sync");
	}

	AnimationProfileEditorWindow::AnimationProfileEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.animationProfiles, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Animation Profiles";
	}

	void AnimationProfileEditorWindow::DrawClipSet(proto::AnimationClipSet& clipSet)
	{
		// Slot -> clip bindings
		if (ImGui::BeginTable("bindings", 4, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthStretch, 0.3f);
			ImGui::TableSetupColumn("Clip", ImGuiTableColumnFlags_WidthStretch, 0.4f);
			ImGui::TableSetupColumn("Play Rate", ImGuiTableColumnFlags_WidthStretch, 0.2f);
			ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthStretch, 0.1f);
			ImGui::TableHeadersRow();

			int removeIndex = -1;
			for (int i = 0; i < clipSet.bindings_size(); ++i)
			{
				auto* binding = clipSet.mutable_bindings(i);
				ImGui::PushID(i);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				int slot = static_cast<int>(binding->slot());
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::Combo("##slot", &slot, s_animationSlotNames, static_cast<int>(std::size(s_animationSlotNames))))
				{
					binding->set_slot(static_cast<uint32>(slot));
				}

				ImGui::TableNextColumn();
				std::string clip = binding->clip();
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::InputText("##clip", &clip))
				{
					binding->set_clip(clip);
				}

				ImGui::TableNextColumn();
				float playRate = binding->play_rate();
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::DragFloat("##playrate", &playRate, 0.01f, 0.01f, 10.0f, "%.2f"))
				{
					binding->set_play_rate(playRate);
				}

				ImGui::TableNextColumn();
				if (ImGui::SmallButton("X"))
				{
					removeIndex = i;
				}

				ImGui::PopID();
			}

			ImGui::EndTable();

			if (removeIndex >= 0)
			{
				clipSet.mutable_bindings()->DeleteSubrange(removeIndex, 1);
			}
		}

		if (ImGui::Button("Add Binding"))
		{
			auto* binding = clipSet.add_bindings();
			binding->set_slot(proto::ANIM_SLOT_IDLE);
			binding->set_clip("");
		}

		// Optional directional movement blend space
		bool useBlendSpace = clipSet.has_movement();
		if (ImGui::Checkbox("Directional Blend Space", &useBlendSpace))
		{
			if (useBlendSpace)
			{
				clipSet.mutable_movement();
			}
			else
			{
				clipSet.clear_movement();
			}
		}
		ImGui::SameLine();
		DrawHelpMarker(
			"When enabled, movement animations come from directional samples instead of the\n"
			"discrete Move slots: the two clips adjacent to the movement direction are blended.\n"
			"Angles are in degrees: 0 = forward, 90 = right, 180 = backward, 270 = left.");

		if (clipSet.has_movement())
		{
			auto* movement = clipSet.mutable_movement();

			float maxBlendAngle = movement->max_blend_angle();
			ImGui::SetNextItemWidth(150.0f);
			if (ImGui::DragFloat("Max Blend Angle", &maxBlendAngle, 1.0f, 0.0f, 180.0f, "%.0f"))
			{
				movement->set_max_blend_angle(maxBlendAngle);
			}
			ImGui::SameLine();
			DrawHelpMarker(
				"Adjacent samples farther apart than this angle are not blended; the nearest\n"
				"sample plays alone instead (avoids blending e.g. Run into RunBack).");

			int removeIndex = -1;
			for (int i = 0; i < movement->clips_size(); ++i)
			{
				auto* sample = movement->mutable_clips(i);
				ImGui::PushID(1000 + i);

				float angle = sample->angle();
				ImGui::SetNextItemWidth(100.0f);
				if (ImGui::DragFloat("##angle", &angle, 1.0f, 0.0f, 359.0f, "%.0f deg"))
				{
					sample->set_angle(angle);
				}

				ImGui::SameLine();
				std::string clip = sample->clip();
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::InputText("##clip", &clip))
				{
					sample->set_clip(clip);
				}

				ImGui::SameLine();
				if (ImGui::SmallButton("X"))
				{
					removeIndex = i;
				}

				ImGui::PopID();
			}

			if (removeIndex >= 0)
			{
				movement->mutable_clips()->DeleteSubrange(removeIndex, 1);
			}

			if (ImGui::Button("Add Sample"))
			{
				auto* sample = movement->add_clips();
				sample->set_angle(0.0f);
				sample->set_clip("");
			}
		}
	}

	void AnimationProfileEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
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

			float fadeDuration = currentEntry.fade_duration();
			ImGui::SetNextItemWidth(150.0f);
			if (ImGui::DragFloat("Fade Duration", &fadeDuration, 0.01f, 0.01f, 2.0f, "%.2f s"))
			{
				currentEntry.set_fade_duration(fadeDuration);
			}
			ImGui::SameLine();
			DrawHelpMarker("Crossfade duration in seconds used when blending between locomotion animations.");
		}

		if (const auto section = ScopedEditorSection("Base Clip Set", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PushID("BaseSet");
			ImGui::TextDisabled("Slots not bound here fall back to the built-in default clip names (Idle, Run, Swim, ...).");
			DrawClipSet(*currentEntry.mutable_base());
			ImGui::PopID();
		}

		if (const auto section = ScopedEditorSection("Condition Overrides", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled(
				"Override sets rebind slots while a condition is active. Resolution priority:\n"
				"Stealth > Swim > Combat (2H/1H/Unarmed) > Combat > Walk; unbound slots fall through.");

			int removeIndex = -1;
			for (int i = 0; i < currentEntry.overrides_size(); ++i)
			{
				auto* overrideSet = currentEntry.mutable_overrides(i);
				ImGui::PushID(i);

				const uint32 condition = overrideSet->condition();
				const char* conditionName = condition < std::size(s_animationConditionNames)
					? s_animationConditionNames[condition] : "Unknown";

				char headerLabel[128];
				snprintf(headerLabel, sizeof(headerLabel), "%s###override%d", conditionName, i);
				if (ImGui::TreeNodeEx(headerLabel, ImGuiTreeNodeFlags_Framed))
				{
					int conditionIndex = static_cast<int>(condition);
					if (ImGui::Combo("Condition", &conditionIndex, s_animationConditionNames,
						static_cast<int>(std::size(s_animationConditionNames))))
					{
						overrideSet->set_condition(static_cast<uint32>(conditionIndex));
					}

					DrawClipSet(*overrideSet->mutable_clips());

					if (ImGui::Button("Remove Override"))
					{
						removeIndex = i;
					}

					ImGui::TreePop();
				}

				ImGui::PopID();
			}

			if (removeIndex >= 0)
			{
				currentEntry.mutable_overrides()->DeleteSubrange(removeIndex, 1);
			}

			if (ImGui::Button("Add Override"))
			{
				auto* overrideSet = currentEntry.add_overrides();
				overrideSet->set_condition(proto::ANIM_COND_WALK);
				overrideSet->mutable_clips();
			}
		}
	}

	void AnimationProfileEditorWindow::OnNewEntry(proto::TemplateManager<proto::AnimationProfiles, proto::AnimationProfileEntry>::EntryType& entry)
	{
		entry.set_name("New Animation Profile");
	}
}
