// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "aura_stacking_category_editor_window.h"
#include "editor_imgui_helpers.h"

namespace mmo
{
	AuraStackingCategoryEditorWindow::AuraStackingCategoryEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.auraStackingCategories, name)
		, m_host(host)
	{
		m_hasToolbarButton = false;
		EditorWindowBase::SetVisible(false);
	}

	void AuraStackingCategoryEditorWindow::DrawDetailsImpl(proto::AuraStackingCategoryEntry& currentEntry)
	{
		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputText("Name", currentEntry.mutable_name());
		}
	}

	void AuraStackingCategoryEditorWindow::OnNewEntry(proto::TemplateManager<proto::AuraStackingCategories, proto::AuraStackingCategoryEntry>::EntryType& entry)
	{
		entry.set_name("New Aura Stacking Category");
	}

	const String& AuraStackingCategoryEditorWindow::EntryDisplayName(const proto::AuraStackingCategoryEntry& entry)
	{
		return entry.name();
	}
}
