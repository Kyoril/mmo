// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// @brief Editor window for managing skill entries (professions and their recipe categories).
	class SkillEditorWindow final
		: public EditorEntryWindowBase<proto::Skills, proto::SkillEntry>
		, public NonCopyable
	{
	public:
		explicit SkillEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~SkillEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Skills, proto::SkillEntry>::EntryType& entry) override;

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
		proto::Project& m_project;
	};
}
