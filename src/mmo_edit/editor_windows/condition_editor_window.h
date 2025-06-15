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
	class ConditionEditorWindow final
		: public EditorEntryWindowBase<proto::Conditions, proto::Condition>
		, public NonCopyable
	{
	public:
		explicit ConditionEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ConditionEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::Condition& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Conditions, proto::Condition>::EntryType& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
	};
}
