// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "render_window_metal.h"
#include "graphics_device_metal.h"

#include <utility>

#include "log/default_log_levels.h"

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
        m_window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0f, 0.0f, width, height) styleMask:(NSWindowStyleMaskClosable | NSWindowStyleMaskTitled | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable) backing:NSBackingStoreBuffered defer:NO];
        
        m_delegate = [[RenderWindowDelegate alloc] init:this];
        m_window.delegate = m_delegate;
        
        [m_window center];
        [m_window makeKeyAndOrderFront:nil];
	}

    RenderWindowMetal::~RenderWindowMetal()
    {
        DestroyNativeWindow();
    }

    void RenderWindowMetal::NotifyClosed()
    {
        Closed();
    }

    void RenderWindowMetal::DestroyNativeWindow()
    {
        if (m_delegate)
        {
            [m_delegate notifyWindowDeleted];
            [m_delegate release];
            m_delegate = nil;
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
	}

	void RenderWindowMetal::SetTitle(const std::string & title)
	{
        m_window.title = [NSString stringWithUTF8String:title.c_str()];
	}
}
