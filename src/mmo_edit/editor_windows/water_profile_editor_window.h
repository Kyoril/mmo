// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

#include <imgui.h>

namespace mmo
{
	/// Allows editing of water profiles: the surface material of a liquid and how it looks and
	/// sounds from below the surface (fog, depth tint, caustics, distortion, audio muffling).
	/// A profile's id is the terrain::WaterType it applies to, so one profile covers every tile
	/// painted with that liquid.
	class WaterProfileEditorWindow final
		: public EditorEntryWindowBase<proto::WaterProfiles, proto::WaterProfile>
		, public NonCopyable
	{
	public:
		explicit WaterProfileEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~WaterProfileEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::WaterProfile& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::WaterProfiles, proto::WaterProfile>::EntryType& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		proto::Project& m_project;
	};
}
