// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Manages the available gossip menus.
	class GossipEditorWindow final
		: public EditorEntryWindowBase<proto::GossipMenus, proto::GossipMenuEntry>
		, public NonCopyable
	{
	public:
		explicit GossipEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~GossipEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::GossipMenuEntry& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::GossipMenus, proto::GossipMenuEntry>::EntryType& entry) override;

	public:
		bool IsDockable() const noexcept override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
	};
}
