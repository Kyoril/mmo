// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	class IAudio;

	/// Allows editing of sound entries which are referenced by id from zones, races
	/// and server-triggered sounds.
	class SoundEditorWindow final
		: public EditorEntryWindowBase<proto::Sounds, proto::SoundEntry>
		, public NonCopyable
	{
	public:
		explicit SoundEditorWindow(const String& name, proto::Project& project, EditorHost& host, IAudio* audio);
		~SoundEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::SoundEntry& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Sounds, proto::SoundEntry>::EntryType& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		IAudio* m_audio;
	};
}
