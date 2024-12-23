// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Manages the available model files in the asset registry.
	class RangeTypeEditorWindow final
		: public EditorEntryWindowBase<proto::Ranges, proto::RangeType>
		, public NonCopyable
	{
	public:
		explicit RangeTypeEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~RangeTypeEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::RangeType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Ranges, proto::RangeType>::EntryType& entry) override;

		const String& EntryDisplayName(const proto::RangeType& entry) override;

	public:
		bool IsDockable() const noexcept override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
	};
}
