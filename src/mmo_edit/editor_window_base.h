// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/signal.h"
#include "base/typedefs.h"

namespace mmo
{
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
		[[nodiscard]] const String& GetName() const noexcept { return m_name; }

		/// Gets whether the window is currently visible.
		[[nodiscard]] virtual bool IsVisible() const noexcept { return m_visible; }

		/// @brief Sets the visibility of this window.
		/// @param value The new visibility. True if the window should be visible.
		virtual void SetVisible(bool value);

		/// @brief Closes the window.
		virtual void Close();

		/// @brief Shows the window.
		virtual void Open();

		/// @brief Called when it's time to draw the window.
		virtual bool Draw() = 0;

	protected:
		String m_name;
		bool m_visible { true };
	};
}
