// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// @brief Editor window for managing item subclass entries.
	class ItemSubclassEditorWindow final
		: public EditorEntryWindowBase<proto::ItemSubclasses, proto::ItemSubclassEntry>
		, public NonCopyable
	{
	public:
		explicit ItemSubclassEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ItemSubclassEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::ItemSubclasses, proto::ItemSubclassEntry>::EntryType& entry) override;

	public:
		bool IsDockable() const override 
		{ 
			return true; 
		}

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override 
		{ 
			return DockDirection::Center; 
		}

	private:
		EditorHost& m_host;
	};
}
