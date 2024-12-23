// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/render_window.h"
#include "render_target_null.h"

namespace mmo
{
	class GraphicsDeviceNull;


	/// A D3D11 implementation of the render window class. Used for rendering contents in a native WinApi window.
	/// Will support using an external window handle instead of the internal one.
	class RenderWindowNull final
		: public RenderWindow
		, public RenderTargetNull
	{
	public:
		RenderWindowNull(GraphicsDeviceNull& device, std::string name, uint16 width, uint16 height, bool fullScreen);

	public:
		// ~Begin RenderTarget
		virtual void Activate() final override;
		virtual void Clear(ClearFlags flags) final override;
		virtual void Resize(uint16 width, uint16 height) final override;
		virtual void Update() final override;
		// ~End RenderTarget

		// ~Begin RenderWindow
		virtual void SetTitle(const std::string& title) final override;
		// ~End RenderWindow
	};
}
