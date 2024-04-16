// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_window_base.h"

#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

namespace mmo
{
	/// Manages the available model files in the asset registry.
	class SpellEditorWindow final
		: public EditorWindowBase
		, public NonCopyable
	{
	public:
		explicit SpellEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~SpellEditorWindow() override = default;

	public:
		/// Draws the viewport window.
		bool Draw() override;

	public:
		bool IsDockable() const noexcept override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const noexcept override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		proto::Project& m_project;
		std::vector<String> m_textures;
		std::map<std::string, TexturePtr> m_iconCache;
	};
}
