// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "log/log_entry.h"

#include <mutex>
#include <vector>

#include "editor_window_base.h"
#include "imgui.h"

namespace mmo
{
	/// This class manages the log window which is displayed to output log messages 
	/// in the UI using Dear ImGUI.
	class LogWindow final
		: public EditorWindowBase
		, public NonCopyable
	{
	public:
		explicit LogWindow();
		~LogWindow() override = default;

	public:
		/// @copydoc EditorWindowBase::Draw
		bool Draw() override;

		/// @copydoc EditorWindowBase::IsDockable 
		bool IsDockable() const noexcept override { return true; }

	private:
		bool m_visible = true;
		int m_selectedItem = -1;
		std::vector<mmo::LogEntry> m_logEntries;
		std::mutex m_logWindowMutex;
		scoped_connection m_logConnection;
		ImGuiTextFilter m_filter;
	};
}