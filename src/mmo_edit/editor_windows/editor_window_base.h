// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"
#include "base/typedefs.h"

namespace mmo
{
	enum class DockDirection
	{
		Top,
		Left,
		Right,
		Bottom,
		Center
	};

	/// @brief Base class of a dockable editor ui window.
	class EditorWindowBase
	{
	public:
		signal<void(EditorWindowBase&, bool)> visibilityChanged;

	public:
		explicit EditorWindowBase(const String& name);
		virtual ~EditorWindowBase() = default;

	public:
		/// @brief Gets the name of the window.
		/// @return Name of the window.
		[[nodiscard]] const String& GetName() const { return m_name; }

		/// Gets whether the window is currently visible.
		[[nodiscard]] virtual bool IsVisible() const { return m_visible; }

		/// @brief Sets the visibility of this window.
		/// @param value The new visibility. True if the window should be visible.
		virtual void SetVisible(bool value);

		/// @brief Closes the window.
		virtual void Close();

		/// @brief Shows the window.
		virtual void Open();

		/// @brief Called when it's time to draw the window.
		virtual bool Draw() = 0;

		virtual bool HasToolbarButton() const { return m_hasToolbarButton; }

		virtual const String& GetToolbarButtonText() const { return m_toolbarButtonText; }

		/// @brief Determines whether the window is dockable.
		/// @return True if the window is dockable, false otherwise.
		[[nodiscard]] virtual bool IsDockable() const { return false; }

		/// @brief Determines whethe the window is resizable.
		/// @return True if the window is resizable.
		[[nodiscard]] virtual bool IsResizable() const { return true; }

		/// @brief Gets the default dock direction of the window.
		///	@return The default dock direction of the window.
		[[nodiscard]] virtual DockDirection GetDefaultDockDirection() const { return DockDirection::Bottom; }

		/// @brief Gets the default dock size of the window.
		[[nodiscard]] virtual float GetDefaultDockSize() const { return 400.0f; }

	protected:
		String m_name;
		bool m_visible { true };
		bool m_hasToolbarButton{ false };
		String m_toolbarButtonText;
	};
}
