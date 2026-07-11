// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Manages the available model files in the asset registry.
	class ZoneEditorWindow final
		: public EditorEntryWindowBase<proto::Zones, proto::ZoneEntry>
		, public NonCopyable
	{
	public:
		explicit ZoneEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~ZoneEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::ZoneEntry& currentEntry) override;

		/// Draws a filtered combo box to pick a sound entry (0 = inherit/none).
		void DrawSoundEntryCombo(const char* label, uint32 currentSoundId, ImGuiTextFilter& filter, const std::function<void(uint32)>& setter);

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		ImGuiTextFilter m_parentZoneFilter;
		ImGuiTextFilter m_owningFactionFilter;
		ImGuiTextFilter m_musicSoundFilter;
		ImGuiTextFilter m_ambienceSoundFilter;
	};
}
