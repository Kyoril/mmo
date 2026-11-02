// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// @brief Editor window for managing pet entries (pets a player can own, tame or summon).
	class PetEditorWindow final
		: public EditorEntryWindowBase<proto::Pets, proto::PetEntry>
		, public NonCopyable
	{
	public:
		explicit PetEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~PetEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Pets, proto::PetEntry>::EntryType& entry) override;

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
