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

		/// @brief Optional icon (e.g. an ICON_FA_* glyph) shown before the toolbar button text.
		///	@return The icon glyph string, or an empty string for no icon.
		virtual const String& GetToolbarButtonIcon() const { return m_toolbarButtonIcon; }

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

		/// @brief Whether this panel supports being collapsed into an edge rail.
		///	Defaults to dockable tool panels (e.g. Data Navigator, Log, Asset Browser).
		[[nodiscard]] virtual bool IsCollapsible() const { return IsDockable(); }

		/// @brief Whether the panel is currently collapsed into its edge rail.
		[[nodiscard]] bool IsCollapsed() const { return m_collapsed; }

		/// @brief Collapses or expands the panel. Expanding requests a dock-location restore.
		void SetCollapsed(bool value)
		{
			if (m_collapsed && !value)
			{
				m_restoreDock = true;
			}
			m_collapsed = value;
		}

		/// @brief Icon glyph (e.g. an ICON_FA_* value) shown for this panel on its edge rail.
		[[nodiscard]] const String& GetPanelIcon() const { return m_panelIcon; }

		/// @brief Remembers the dock node the panel last lived in, so it can be restored on expand.
		[[nodiscard]] unsigned int GetLastDockId() const { return m_lastDockId; }
		void SetLastDockId(unsigned int id) { m_lastDockId = id; }

		/// @brief User-adjusted size of the hover flyout along its resizable axis (width for
		///	side-docked panels, height for top/bottom-docked panels). 0 means "use the default".
		[[nodiscard]] float GetFlyoutSize() const { return m_flyoutSize; }
		void SetFlyoutSize(float value) { m_flyoutSize = value; }

		/// @brief Returns true once after expanding, signalling that the dock location should be restored.
		bool ConsumeRestoreDock()
		{
			const bool restore = m_restoreDock;
			m_restoreDock = false;
			return restore;
		}

	protected:
		String m_name;
		bool m_visible { true };
		bool m_hasToolbarButton{ false };
		String m_toolbarButtonText;
		String m_toolbarButtonIcon;
		String m_panelIcon;
		bool m_collapsed { false };
		bool m_restoreDock { false };
		unsigned int m_lastDockId { 0 };
		float m_flyoutSize { 0.0f };
	};
}
