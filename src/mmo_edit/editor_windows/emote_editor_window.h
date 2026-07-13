// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	class PreviewProviderManager;

	/// @brief Editor window for managing animated emote definitions (one-shots, poses,
	///	moods and pose variants).
	class EmoteEditorWindow final
		: public EditorEntryWindowBase<proto::Emotes, proto::EmoteEntry>
		, public NonCopyable
	{
	public:
		explicit EmoteEditorWindow(const String& name, proto::Project& project, EditorHost& host, PreviewProviderManager& previewManager);
		~EmoteEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::Emotes, proto::EmoteEntry>::EntryType& entry) override;

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
		PreviewProviderManager& m_previewManager;
	};
}
