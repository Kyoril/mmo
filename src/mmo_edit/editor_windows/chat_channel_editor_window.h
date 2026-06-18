// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	/// @brief Editor window for managing well-known chat channel definitions.
	class ChatChannelEditorWindow final
		: public EditorEntryWindowBase<proto::ChatChannels, proto::ChatChannelEntry>
		, public NonCopyable
	{
	public:
		explicit ChatChannelEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ChatChannelEditorWindow() override = default;

	private:
		void DrawDetailsImpl(EntryType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::ChatChannels, proto::ChatChannelEntry>::EntryType& entry) override;

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
