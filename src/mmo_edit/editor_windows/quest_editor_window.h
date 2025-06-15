// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Manages the available model files in the asset registry.
	class QuestEditorWindow final
		: public EditorEntryWindowBase<proto::Quests, proto::QuestEntry>
		, public NonCopyable
	{
	public:
		explicit QuestEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~QuestEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::QuestEntry& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Quests, proto::QuestEntry>::EntryType& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
	};
}
