// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "attribute_editor_window.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "game/attribute.h"
#include "log/default_log_levels.h"

namespace mmo
{
	AttributeEditorWindow::AttributeEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.attributes, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Attributes";
	}

	uint32 AttributeEditorWindow::FindLowestFreeId() const
	{
		for (uint32 id = 0; id < MaxAttributes; ++id)
		{
			if (m_manager.getById(id) == nullptr)
			{
				return id;
			}
		}

		return MaxAttributes;
	}

	void AttributeEditorWindow::OnNewEntry(proto::TemplateManager<proto::Attributes, proto::AttributeEntry>::EntryType& entry)
	{
		// The manager hands out ever increasing ids, but attribute ids are slots: replace the new
		// entry by one in the lowest free slot. Only the slot below MaxAttributes is valid.
		const uint32 addedId = entry.id();
		const uint32 freeId = FindLowestFreeId();
		if (freeId == addedId)
		{
			entry.set_name("New Attribute");
			return;
		}

		// 'entry' is destroyed here and must not be used afterwards
		m_manager.remove(addedId);

		if (freeId >= MaxAttributes)
		{
			ELOG("Can not add another attribute: all " << MaxAttributes << " attribute slots are in use");
			return;
		}

		if (auto* added = m_manager.add(freeId))
		{
			added->set_name("New Attribute");
		}
	}

	void AttributeEditorWindow::ChangeEntryId(const uint32 oldId, const uint32 newId)
	{
		auto& entries = *m_manager.getTemplates().mutable_entry();

		int index = -1;
		for (int i = 0; i < entries.size(); ++i)
		{
			if (entries.Get(i).id() == oldId)
			{
				index = i;
				break;
			}
		}

		if (index < 0 || newId >= MaxAttributes || m_manager.getById(newId) != nullptr)
		{
			return;
		}

		// The manager indexes entries by id, so re-add the entry under its new id...
		const proto::AttributeEntry copy = entries.Get(index);
		m_manager.remove(oldId);

		auto* added = m_manager.add(newId);
		if (added == nullptr)
		{
			return;
		}

		added->CopyFrom(copy);
		added->set_id(newId);

		// ...and move it back to its old position, as the list order is the display order. Swapping
		// keeps the element objects in place, so the manager's id lookup stays valid.
		for (int i = entries.size() - 1; i > index; --i)
		{
			entries.SwapElements(i, i - 1);
		}

		m_currentItem = index;
		m_idEditEntry = newId;
		m_idEditValue = static_cast<int>(newId);
	}

	void AttributeEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		if (m_idEditEntry != currentEntry.id())
		{
			m_idEditEntry = currentEntry.id();
			m_idEditValue = static_cast<int>(currentEntry.id());
		}

		ImGui::TextWrapped(
			"Adding or removing an attribute refunds every character's spent attribute points on their next login. "
			"Renaming an attribute or changing its texts does not.");
		ImGui::Spacing();

		if (currentEntry.id() >= MaxAttributes)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Invalid id %u: attribute ids must be smaller than %u. Change the id below.", currentEntry.id(), MaxAttributes);
		}

		bool applyIdChange = false;

		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputText("Name", currentEntry.mutable_name());

			ImGui::SetNextItemWidth(120.0f);
			ImGui::InputInt("ID", &m_idEditValue);
			ImGui::SameLine();

			const bool idChanged = m_idEditValue != static_cast<int>(currentEntry.id());
			const bool idInRange = m_idEditValue >= 0 && m_idEditValue < static_cast<int>(MaxAttributes);
			const bool idFree = idInRange && m_manager.getById(static_cast<uint32>(m_idEditValue)) == nullptr;

			ImGui::BeginDisabled(!idChanged || !idFree);
			if (DrawPrimaryButton("Change ID"))
			{
				applyIdChange = true;
			}
			ImGui::EndDisabled();

			if (idChanged && !idInRange)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "The id must be between 0 and %u.", MaxAttributes - 1);
			}
			else if (idChanged && !idFree)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Id %d is already used by another attribute.", m_idEditValue);
			}

			ImGui::TextDisabled(
				"The id is the attribute's slot. Class / unit class base values, stat sources, item stats and "
				"ModStat auras reference attributes by id and are NOT updated when the id changes.");
		}

		if (const auto section = ScopedEditorSection("Texts", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputTextMultiline("Description", currentEntry.mutable_description(), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f));
			ImGui::TextDisabled("Shown as tooltip of the attribute in the character window.");

			ImGui::InputTextWithHint("Level Up Text", "You have gained %d stamina.", currentEntry.mutable_leveluptext());
			ImGui::TextDisabled("Chat line printed on level up. %%d is replaced by the gained amount. Empty prints nothing.");
		}

		// Last: changing the id destroys 'currentEntry'
		if (applyIdChange)
		{
			ChangeEntryId(currentEntry.id(), static_cast<uint32>(m_idEditValue));
		}
	}
}
