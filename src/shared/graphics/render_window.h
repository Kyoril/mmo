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
	};

	typedef std::shared_ptr<RenderWindow> RenderWindowPtr;
}
