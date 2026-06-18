// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Manages the available aura stacking categories in the project.
	class AuraStackingCategoryEditorWindow final
		: public EditorEntryWindowBase<proto::AuraStackingCategories, proto::AuraStackingCategoryEntry>
		, public NonCopyable
	{
	public:
		explicit AuraStackingCategoryEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~AuraStackingCategoryEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::AuraStackingCategoryEntry& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::AuraStackingCategories, proto::AuraStackingCategoryEntry>::EntryType& entry) override;

		const String& EntryDisplayName(const proto::AuraStackingCategoryEntry& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
	};
}
