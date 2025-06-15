// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Manages the available model files in the asset registry.
	class ObjectDisplayEditorWindow final
		: public EditorEntryWindowBase<proto::ObjectDisplayData, proto::ObjectDisplayEntry>
		, public NonCopyable
	{
	public:
		explicit ObjectDisplayEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ObjectDisplayEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(EntryType& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
	};
}
