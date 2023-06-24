// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "render_window_metal.h"
#include "graphics_device_metal.h"

#include <utility>

#include "log/default_log_levels.h"

#import <Cocoa/Cocoa.h>

@interface RenderWindowDelegate : NSObject
@end

@implementation RenderWindowDelegate
{
    mmo::RenderWindowMetal* m_cppWindow;
}

- (id)init:(mmo::RenderWindowMetal*)window
{
    if (self = [super init])
    {
        m_cppWindow = window;
    }
    
    return self;
}

- (void)dealloc
{
    [super dealloc];
}

- (void)notifyWindowDeleted
{
    m_cppWindow = nullptr;
}

- (void)windowWillClose:(NSWindow*)window
{
    if (m_cppWindow)
    {
        m_cppWindow->NotifyClosed();
    }
}

@end

namespace mmo
{
	RenderWindowMetal::RenderWindowMetal(GraphicsDeviceMetal & device, std::string name, uint16 width, uint16 height, bool fullScreen)
		: RenderWindow(std::move(name), width, height)
		, RenderTargetMetal(device)
	{
        CGRect contentRect = { 0.0, 0.0, static_cast<double>(width), static_cast<double>(height) };
        
        m_window = NS::Window::alloc()->init(contentRect, NS::WindowStyleMaskClosable | NS::WindowStyleMaskTitled | NS::WindowStyleMaskMiniaturizable | NS::WindowStyleMaskResizable, NS::BackingStoreBuffered, false);
        
        m_delegate = [[RenderWindowDelegate alloc] init:this];
        m_window->setDelegate(m_delegate);
        
        m_mtkView = MTK::View::alloc()->init(contentRect, m_device.GetDevice());
        m_mtkView->setColorPixelFormat( MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB );
        m_mtkView->setClearColor( MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0 ) );
        m_mtkView->setDelegate(this);
        
        m_window->setContentView(m_mtkView);
        m_window->makeKeyAndOrderFront(nullptr);
	}

    RenderWindowMetal::~RenderWindowMetal()
    {
        DestroyNativeWindow();
    }

    void RenderWindowMetal::drawInMTKView( MTK::View* pView )
    {
        NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();

        MTL::CommandBuffer* pCmd = m_device.GetCommandQueue()->commandBuffer();
        MTL::RenderPassDescriptor* pRpd = pView->currentRenderPassDescriptor();
        MTL::RenderCommandEncoder* pEnc = pCmd->renderCommandEncoder( pRpd );
        pEnc->endEncoding();
        pCmd->presentDrawable( pView->currentDrawable() );
        pCmd->commit();

        pPool->release();
    }

    void RenderWindowMetal::NotifyClosed()
    {
        Closed();
    }

    void RenderWindowMetal::DestroyNativeWindow()
    {
        if (m_mtkView)
        {
            m_mtkView->release();
            m_mtkView = nullptr;
        }
        
        if (m_window)
        {
            m_window->release();
            m_window = nullptr;
        }
    }

	void RenderWindowMetal::Activate()
	{
		RenderTarget::Activate();
		RenderTargetMetal::Activate();

		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
	}

	void RenderWindowMetal::Clear(ClearFlags flags)
	{
		RenderTargetMetal::Clear(flags);
        
	}

	void RenderWindowMetal::Resize(uint16 width, uint16 height)
	{
		if (width == 0) return;
		if (height == 0) return;
		if (width == m_width && height == m_height) return;
	}

	void RenderWindowMetal::Update()
	{
        m_mtkView->draw();
	}

	void RenderWindowMetal::SetTitle(const std::string & title)
	{
        m_window->setTitle(NS::String::string(title.c_str(), NS::UTF8StringEncoding));
	}
}
