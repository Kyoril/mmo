// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "log/log_entry.h"

#include <string>
#include <vector>
#include <mutex>

#include "imgui.h"

namespace mmo
{
	/// This class manages the log window which is displayed to output log messages 
	/// in the UI using Dear ImGUI.
	class LogWindow final
		: public NonCopyable
	{
	public:
		explicit LogWindow();

	public:
		// ImGui draw functions

		/// Renders the log window using Dear ImGui.
		bool Draw();
		/// Renders the view menu item.
		bool DrawViewMenuItem();

	public:
		// Common methods

		/// Makes the log window visible on screen.
		void Show();
		/// Gets whether the window is visible.
		inline bool IsVisible() const { return m_visible; }

	private:
		bool m_visible = true;
		int m_selectedItem = -1;
		std::vector<mmo::LogEntry> m_logEntries;
		std::mutex m_logWindowMutex;
		scoped_connection m_logConnection;
		ImGuiTextFilter m_filter;
	};
}