// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "editor_entry_window_base.h"
#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

#include <imgui.h>
#include <map>

namespace mmo
{
	/// Edits talent tabs and their freeform node graphs.
	class TalentEditorWindow final
		: public EditorEntryWindowBase<proto::TalentTabs, proto::TalentTabEntry>
		, public NonCopyable
	{
	public:
		explicit TalentEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~TalentEditorWindow() override = default;

		bool IsDockable() const override { return true; }
		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		void DrawDetailsImpl(proto::TalentTabEntry& currentEntry) override;
		void DrawTalentCanvas(proto::TalentTabEntry& currentTab);
		void DrawTalentNodeEditor(proto::TalentEntry& talent);
		void CreateNewTalent(uint32 tabId, const ImVec2& position);
		void OnNewEntry(proto::TemplateManager<proto::TalentTabs, proto::TalentTabEntry>::EntryType& entry) override;
		ImVec2 GetTalentPosition(const proto::TalentEntry& talent) const;
		TexturePtr GetTalentIcon(const proto::TalentEntry& talent);
		/// Builds a human readable label for a talent (id + first rank spell name) for use in lists.
		std::string GetTalentDisplayName(const proto::TalentEntry& talent) const;
		/// Adds sourceTalentId as a prerequisite of targetTalentId, requiring all of its ranks. No-op on
		/// invalid ids, self-reference or duplicate prerequisites.
		void AddPrerequisite(uint32 targetTalentId, uint32 sourceTalentId);

	private:
		EditorHost& m_host;
		std::map<std::string, TexturePtr> m_iconCache;
		int32 m_selectedTalentId = -1;
		float m_canvasZoom = 0.75f;
		ImVec2 m_canvasPan = ImVec2(20.0f, 20.0f);
		bool m_showGrid = true;
		bool m_snapToGrid = false;
		int m_gridSize = 20;
		int32 m_draggingTalentId = -1;
		ImVec2 m_dragNodeOrigin = ImVec2(0.0f, 0.0f);
		ImVec2 m_dragMouseOrigin = ImVec2(0.0f, 0.0f);
		bool m_pickingPrerequisite = false;
	};
}
