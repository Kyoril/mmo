// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

#include <imgui.h>

namespace mmo
{
	/// Allows editing of surface types which are resolved from materials and referenced
	/// by footstep sound playback.
	class SurfaceTypeEditorWindow final
		: public EditorEntryWindowBase<proto::SurfaceTypes, proto::SurfaceType>
		, public NonCopyable
	{
	public:
		explicit SurfaceTypeEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~SurfaceTypeEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::SurfaceType& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::SurfaceTypes, proto::SurfaceType>::EntryType& entry) override;

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		proto::Project& m_project;
		ImGuiTextFilter m_footstepSoundFilter;
	};
}
