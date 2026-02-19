// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// @brief Editor window for managing proficiency entries.
	class ProficiencyEditorWindow final
		: public EditorEntryWindowBase<proto::Proficiencies, proto::ProficiencyEntry>
		, public NonCopyable
	{
	public:
		explicit ProficiencyEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ProficiencyEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Proficiencies, proto::ProficiencyEntry>::EntryType& entry) override;

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
