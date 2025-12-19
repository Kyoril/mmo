// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	class MapEditorWindow final
		: public EditorEntryWindowBase<proto::Maps, proto::MapEntry>
		, public NonCopyable
	{
	public:
		explicit MapEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~MapEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::MapEntry& currentEntry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
	};
}
