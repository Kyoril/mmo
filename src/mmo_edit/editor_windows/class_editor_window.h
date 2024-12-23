// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editor_entry_window_base.h"
#include "base/non_copyable.h"

#include "editor_window_base.h"

#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Manages the available model files in the asset registry.
	class ClassEditorWindow final
		: public EditorEntryWindowBase<proto::Classes, proto::ClassEntry>
		, public NonCopyable
	{
	public:
		explicit ClassEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ClassEditorWindow() override = default;

	public:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Classes, proto::ClassEntry>::EntryType& entry) override;

	public:
		bool IsDockable() const noexcept override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		proto::Project& m_project;
	};
}
