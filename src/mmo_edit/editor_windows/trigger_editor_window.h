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
	class TriggerEditorWindow final
		: public EditorEntryWindowBase<proto::Triggers, proto::TriggerEntry>
		, public NonCopyable
	{
	public:
		explicit TriggerEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~TriggerEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::TriggerEntry& currentEntry) override;

	public:
		bool IsDockable() const noexcept override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		ImGuiTextFilter m_parentZoneFilter;
	};
}
