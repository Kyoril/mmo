// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"
#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Editor window for managing lock type entries.
	/// Lock types are referenced by OpenLock spell effects (miscvaluea) and by
	/// world objects (data[0] = default lock type, data[1] = post-unlock lock type).
	class LockTypeEditorWindow final
		: public EditorEntryWindowBase<proto::LockTypes, proto::LockTypeEntry>
		, public NonCopyable
	{
	public:
		explicit LockTypeEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~LockTypeEditorWindow() override = default;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		void OnNewEntry(EntryType& entry) override;
		void DrawDetailsImpl(EntryType& currentEntry) override;

	private:
		EditorHost& m_host;
	};
}
