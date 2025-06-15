// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editor_entry_window_base.h"
#include "base/non_copyable.h"

#include "editor_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	class ObjectEditorWindow final
		: public EditorEntryWindowBase<proto::Objects, proto::ObjectEntry>
		, public NonCopyable
	{
	public:
		explicit ObjectEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ObjectEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::ObjectEntry& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Objects, proto::ObjectEntry>::EntryType& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		proto::Project& m_project;
	};
}
