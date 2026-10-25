// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "render_target.h"

#include "base/signal.h"

namespace mmo
{
	/// Base class of a render window. A render window is a special type of render target as it represents a
	/// native platform window, whose content rect will be rendered to.
	class RenderWindow
		: public RenderTarget
	{
	public:
		/// Fired when the window is closed.
		signal<void()> Closed;
		/// Fired when the window was resized.
		signal<void(uint16 width, uint16 height)> Resized;

	public:
		RenderWindow(std::string name, uint16 width, uint16 height);
		virtual ~RenderWindow() = default;

	public:
		virtual void SetTitle(const std::string& title) = 0;

		/// @brief Takes the window off the screen, giving the display back to the desktop.
		/// @remarks Needed before anything else may show UI of its own: a backend running in
		///          exclusive fullscreen owns the output, so a message box raised over it can
		///          end up invisible behind the fullscreen surface. Backends that never take
		///          the display exclusively do not need to do anything here.
		virtual void Hide() {}

		/// @brief Switches between a regular window and a borderless window covering a monitor.
		/// @remarks Takes effect immediately; the Resized signal reports the new size once the
		///          swap chain follows. Backends that cannot move their window do nothing.
		/// @param fullscreenWindow True for a borderless window covering the whole monitor.
		/// @param monitorIndex Index into GraphicsDevice::GetDisplayMonitors. Out of range means the
		///        primary monitor.
		/// @param width Client width in windowed mode. Ignored for a fullscreen window.
		/// @param height Client height in windowed mode. Ignored for a fullscreen window.
		virtual void SetDisplayMode(bool fullscreenWindow, uint32 monitorIndex, uint16 width, uint16 height) {}

		/// @brief Returns whether the window is the one the player is using right now: in the
		///        foreground and not minimized. Used to throttle the game while in the background.
		[[nodiscard]] virtual bool HasFocus() const { return true; }
	};

	typedef std::shared_ptr<RenderWindow> RenderWindowPtr;
}
