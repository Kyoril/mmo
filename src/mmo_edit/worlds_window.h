// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "imgui.h"

namespace mmo
{
	class Project;

	/// Manages the game viewport window in the editor.
	class WorldsWindow final
		: public NonCopyable
	{
	public:
		explicit WorldsWindow(Project& project);

	public:
		/// Draws the viewport window.
		bool Draw();
		/// Renders the view menu item.
		bool DrawViewMenuItem();

	public:
		/// Makes the viewport window visible.
		void Show() noexcept { m_visible = true; }
		/// Determines whether the viewport window is currently visible.
		bool IsVisible() const noexcept { return m_visible; }
				
	private:
		Project& m_project;
		bool m_visible;
		ImGuiTextFilter m_worldsFilter;
	};
}
