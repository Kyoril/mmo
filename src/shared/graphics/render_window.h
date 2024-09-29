// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
		RenderWindow(std::string name, uint16 width, uint16 height) noexcept;
		virtual ~RenderWindow() noexcept = default;

	public:
		virtual void SetTitle(const std::string& title) = 0;
	};

	typedef std::shared_ptr<RenderWindow> RenderWindowPtr;
}
