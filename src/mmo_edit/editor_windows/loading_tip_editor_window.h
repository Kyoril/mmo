// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Allows editing of the tips the client shows on its loading screen, including their
	/// per-locale translations.
	class LoadingTipEditorWindow final
		: public EditorEntryWindowBase<proto::LoadingTips, proto::LoadingTip>
		, public NonCopyable
	{
	public:
		explicit LoadingTipEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~LoadingTipEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::LoadingTip& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::LoadingTips, proto::LoadingTip>::EntryType& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
	};
}
