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
	class ItemEditorWindow final
		: public EditorEntryWindowBase<proto::Items, proto::ItemEntry>
		, public NonCopyable
	{
	public:
		explicit ItemEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ItemEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(EntryType& entry) override;

		/// Opens a save dialog and writes the given item to a JSON file.
		void ExportItemToFile(const EntryType& entry);

		/// Opens an open dialog, validates the chosen JSON file and, on success,
		/// overwrites the given item with its contents (keeping the original ID).
		void ImportItemFromFile(EntryType& entry);

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		std::vector<String> m_textures;
		std::map<std::string, TexturePtr> m_iconCache;

		/// Message shown in the import/export result popup.
		String m_importExportMessage;
		/// Whether the last import/export result represents an error (controls coloring).
		bool m_importExportFailed = false;
		/// Set to true to open the import/export result popup on the next frame.
		bool m_openImportExportPopup = false;
	};
}
