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
	class ItemDisplayEditorWindow final
		: public EditorEntryWindowBase<proto::ItemDisplayData, proto::ItemDisplayEntry>
		, public NonCopyable
	{
	public:
		explicit ItemDisplayEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ItemDisplayEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(EntryType& entry) override;

	public:
		bool IsDockable() const noexcept override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		std::vector<String> m_textures;
		std::map<std::string, TexturePtr> m_iconCache;
	};
}
