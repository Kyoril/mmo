// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "preview_providers/preview_provider_manager.h"
#include "configuration.h"
#include "proto_data/project.h"

#include <memory>

namespace mmo
{
	class ItemDisplayPreview;

	/// Manages the available model files in the asset registry.
	class ItemDisplayEditorWindow final
		: public EditorEntryWindowBase<proto::ItemDisplayData, proto::ItemDisplayEntry>
		, public NonCopyable
	{
	public:
		explicit ItemDisplayEditorWindow(const String& name, proto::Project& project, EditorHost& host, PreviewProviderManager& previewManager, Configuration& config);
		~ItemDisplayEditorWindow() override;

	private:
		bool Draw() override;

		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(EntryType& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		PreviewProviderManager& m_previewManager;
		Configuration& m_config;
		std::unique_ptr<ItemDisplayPreview> m_preview;
	};
}
