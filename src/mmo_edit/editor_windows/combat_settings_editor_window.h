// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editor_window_base.h"
#include "base/non_copyable.h"
#include "proto_data/project.h"

namespace mmo
{
	class EditorHost;

	/// @brief Editor window for editing combat formula settings.
	/// Unlike other editor windows that manage lists of entries, this window edits
	/// a single CombatSettings object that controls all combat formula parameters.
	class CombatSettingsEditorWindow final
		: public EditorWindowBase
		, public NonCopyable
	{
	public:
		/// @brief Creates a new CombatSettingsEditorWindow instance.
		/// @param name The window name.
		/// @param project The project that holds the combat settings.
		/// @param host The editor host.
		explicit CombatSettingsEditorWindow(const String& name, proto::Project& project, EditorHost& host);

		/// @brief Default destructor.
		~CombatSettingsEditorWindow() override = default;

	public:
		/// @brief Draws the window content.
		/// @return True if the window requests a redraw.
		bool Draw() override;

		/// @brief Determines whether the window is dockable.
		/// @return True.
		bool IsDockable() const override { return true; }

		/// @brief Gets the default dock direction.
		/// @return Center dock direction.
		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		/// @brief Draws a float input field for a combat setting.
		/// @param label The display label.
		/// @param value The current value.
		/// @param defaultValue The default value to show as hint.
		/// @return True if the value was changed.
		bool DrawFloatSetting(const char* label, float& value, float defaultValue);

		/// @brief Draws a uint32 input field for a combat setting.
		/// @param label The display label.
		/// @param value The current value.
		/// @param defaultValue The default value to show as hint.
		/// @return True if the value was changed.
		bool DrawUint32Setting(const char* label, uint32& value, uint32 defaultValue);

	private:
		EditorHost& m_host;
		proto::Project& m_project;
	};
}
