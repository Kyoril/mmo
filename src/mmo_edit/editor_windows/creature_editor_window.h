// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editor_entry_window_base.h"
#include "base/non_copyable.h"

#include "editor_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	class CreatureEditorWindow final
		: public EditorEntryWindowBase<proto::Units, proto::UnitEntry>
		, public NonCopyable
	{
	public:
		explicit CreatureEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~CreatureEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Units, proto::UnitEntry>::EntryType& entry) override;

	public:
		bool IsDockable() const noexcept override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		proto::Project& m_project;
	};
}
