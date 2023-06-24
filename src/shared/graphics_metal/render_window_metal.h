// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/render_window.h"
#include "render_target_metal.h"

#include "Metal/Metal.hpp"
#include "AppKit/AppKit.hpp"
#include "MetalKit/MetalKit.hpp"

namespace mmo
{
	class GraphicsDeviceMetal;

	/// A D3D11 implementation of the render window class. Used for rendering contents in a native WinApi window.
	/// Will support using an external window handle instead of the internal one.
	class RenderWindowMetal final
		: public RenderWindow
		, public RenderTargetMetal
        , public MTK::ViewDelegate
	{
	public:
        RenderWindowMetal(GraphicsDeviceMetal& device, std::string name, uint16 width, uint16 height, bool fullScreen);
        ~RenderWindowMetal();
        
        virtual void drawInMTKView( MTK::View* pView ) override;
        
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
        
    public:
        void NotifyClosed();
        
    private:
        void DestroyNativeWindow();
        
    private:
        NS::Window* m_window;
        MTK::View* m_mtkView;
        id m_delegate;
	};
}
