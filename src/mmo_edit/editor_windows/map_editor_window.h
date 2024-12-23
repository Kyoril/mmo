// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_window_base.h"

#include "editor_host.h"
#include "proto_data/project.h"

namespace mmo
{
	class MapEditorWindow final
		: public EditorWindowBase
		, public NonCopyable
	{
	public:
		explicit MapEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~MapEditorWindow() override = default;

	public:
		/// Draws the viewport window.
		bool Draw() override;

	public:
		bool IsDockable() const noexcept override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		proto::Project& m_project;
	};
}
