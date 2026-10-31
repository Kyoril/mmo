// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "render_window_metal.h"

#include "log/default_log_levels.h"

#import <AppKit/AppKit.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
	/// Debugging aid: with MMO_METAL_CAPTURE_DIR set, every MMO_METAL_CAPTURE_INTERVAL'th presented
	/// frame (default 300) is written to that directory as a BMP, since screen recording tools
	/// cannot always see the window.
	struct FrameCaptureSettings
	{
		std::string directory;
		uint64_t interval = 300;

		FrameCaptureSettings()
		{
			if (const char* dir = std::getenv("MMO_METAL_CAPTURE_DIR"))
			{
				directory = dir;
			}
			if (const char* interval = std::getenv("MMO_METAL_CAPTURE_INTERVAL"))
			{
				this->interval = std::max<uint64_t>(1, std::strtoull(interval, nullptr, 10));
			}
		}
	};

	const FrameCaptureSettings& GetFrameCaptureSettings()
	{
		static const FrameCaptureSettings s_settings;
		return s_settings;
	}

	void WriteBmp(const std::string& path, const uint8_t* bgra, const uint32_t width, const uint32_t height, const uint32_t rowPitch)
	{
		FILE* file = std::fopen(path.c_str(), "wb");
		if (!file)
		{
			return;
		}

		const uint32_t imageSize = width * height * 4;
		const uint32_t fileSize = 54 + imageSize;
		uint8_t header[54] = { 'B', 'M' };
		auto put32 = [&header](const size_t offset, const uint32_t value)
		{
			header[offset] = value & 0xFF;
			header[offset + 1] = (value >> 8) & 0xFF;
			header[offset + 2] = (value >> 16) & 0xFF;
			header[offset + 3] = (value >> 24) & 0xFF;
		};
		put32(2, fileSize);
		put32(10, 54);
		put32(14, 40);
		put32(18, width);
		put32(22, static_cast<uint32_t>(-static_cast<int32_t>(height)));
		header[26] = 1;
		header[28] = 32;
		put32(34, imageSize);
		std::fwrite(header, 1, sizeof(header), file);
		for (uint32_t y = 0; y < height; ++y)
		{
			std::fwrite(bgra + y * rowPitch, 1, width * 4, file);
		}
		std::fclose(file);
	}
}

/// Content view of the game window. It hosts the Metal layer and swallows key events, which the
/// event loop has already translated: without it, every key press would make the system beep.
@interface MMOMetalView : NSView
@end

@implementation MMOMetalView

- (BOOL)acceptsFirstResponder
{
	return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
	return YES;
}

- (BOOL)wantsUpdateLayer
{
	return YES;
}

- (void)keyDown:(NSEvent*)event
{
}

- (void)keyUp:(NSEvent*)event
{
}

- (void)flagsChanged:(NSEvent*)event
{
}

@end

@interface MMOWindowDelegate : NSObject<NSWindowDelegate>
@property (nonatomic, assign) mmo::RenderWindowMetal* owner;
@end

@implementation MMOWindowDelegate

- (void)windowDidResize:(NSNotification*)notification
{
	if (self.owner)
	{
		self.owner->OnContentResized();
	}
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
	if (self.owner)
	{
		self.owner->OnCloseRequested();
	}

	// The game decides when it closes; it terminates the event loop.
	return NO;
}

@end

namespace mmo
{
	RenderWindowMetal::RenderWindowMetal(GraphicsDeviceMetal& device, std::string name, const uint16 width, const uint16 height, const bool fullScreen)
		: RenderWindow(std::move(name), width, height)
		, m_device(device)
		, m_fullScreen(fullScreen)
	{
		NSRect contentRect = NSMakeRect(0, 0, width, height);
		NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
		if (fullScreen)
		{
			contentRect = [[NSScreen mainScreen] frame];
			style = NSWindowStyleMaskBorderless;
		}

		m_window = [[NSWindow alloc] initWithContentRect:contentRect styleMask:style backing:NSBackingStoreBuffered defer:NO];
		m_window.releasedWhenClosed = NO;
		m_window.acceptsMouseMovedEvents = YES;
		m_window.title = @"MMORPG";

		m_layer = [CAMetalLayer layer];
		m_layer.device = device.GetDevice();
		m_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		// Frame captures (see FrameCaptureSettings) copy from the back buffer.
		m_layer.framebufferOnly = GetFrameCaptureSettings().directory.empty() ? YES : NO;
		m_layer.contentsScale = 1.0;
		m_layer.maximumDrawableCount = 3;
		m_layer.displaySyncEnabled = device.IsVSyncEnabled() ? YES : NO;

		m_view = [[MMOMetalView alloc] initWithFrame:NSMakeRect(0, 0, contentRect.size.width, contentRect.size.height)];
		m_view.layer = m_layer;
		m_view.wantsLayer = YES;
		m_view.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
		m_window.contentView = m_view;

		m_delegate = [[MMOWindowDelegate alloc] init];
		m_delegate.owner = this;
		m_window.delegate = m_delegate;

		if (fullScreen)
		{
			[m_window setLevel:NSNormalWindowLevel];
			[NSApp setPresentationOptions:NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar];
		}
		else
		{
			[m_window center];
		}

		const NSSize contentSize = [m_view bounds].size;
		m_width = static_cast<uint16>(contentSize.width);
		m_height = static_cast<uint16>(contentSize.height);
		m_layer.drawableSize = CGSizeMake(m_width, m_height);
		CreateDepthBuffer();

		[m_window makeKeyAndOrderFront:nil];
		[m_window makeFirstResponder:m_view];
		[NSApp activateIgnoringOtherApps:YES];
	}

	RenderWindowMetal::~RenderWindowMetal()
	{
		if (m_delegate)
		{
			m_delegate.owner = nullptr;
		}

		m_window.delegate = nil;
		[m_window orderOut:nil];
		m_window = nil;
	}

	void RenderWindowMetal::CreateDepthBuffer()
	{
		if (m_depthTexture)
		{
			m_device.ForgetTexture(m_depthTexture);
		}

		MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:std::max<uint16>(1, m_width) height:std::max<uint16>(1, m_height) mipmapped:NO];
		descriptor.usage = MTLTextureUsageRenderTarget;
		descriptor.storageMode = MTLStorageModePrivate;
		m_depthTexture = [m_device.GetDevice() newTextureWithDescriptor:descriptor];
		m_depthTexture.label = @"Window depth";
	}

	id<MTLTexture> RenderWindowMetal::AcquireBackBuffer()
	{
		if (!m_drawable)
		{
			m_drawable = [m_layer nextDrawable];
		}

		return m_drawable ? m_drawable.texture : nil;
	}

	void RenderWindowMetal::Activate()
	{
		RenderTarget::Activate();

		const id<MTLTexture> colors[1] = { AcquireBackBuffer() };
		m_device.SetRenderTargetTextures(colors, colors[0] ? 1 : 0, m_depthTexture);
		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
	}

	void RenderWindowMetal::Clear(const ClearFlags flags)
	{
		m_device.ClearRenderTexture(AcquireBackBuffer(), m_depthTexture, flags);
	}

	void RenderWindowMetal::Resize(const uint16 width, const uint16 height)
	{
		if (width == 0 || height == 0)
		{
			return;
		}

		if (!m_fullScreen)
		{
			[m_window setContentSize:NSMakeSize(width, height)];
		}

		m_pendingWidth = width;
		m_pendingHeight = height;
		m_resizePending = true;
	}

	void RenderWindowMetal::OnContentResized()
	{
		const NSSize size = [m_view bounds].size;
		if (size.width < 1 || size.height < 1)
		{
			return;
		}

		m_pendingWidth = static_cast<uint16>(size.width);
		m_pendingHeight = static_cast<uint16>(size.height);
		m_resizePending = m_pendingWidth != m_width || m_pendingHeight != m_height;
	}

	void RenderWindowMetal::OnCloseRequested()
	{
		Closed();
	}

	void RenderWindowMetal::ApplyPendingResize()
	{
		if (!m_resizePending)
		{
			return;
		}

		m_resizePending = false;
		if (m_pendingWidth == m_width && m_pendingHeight == m_height)
		{
			return;
		}

		m_width = m_pendingWidth;
		m_height = m_pendingHeight;
		m_layer.drawableSize = CGSizeMake(m_width, m_height);
		CreateDepthBuffer();

		Resized(m_width, m_height);
	}

	void RenderWindowMetal::Update()
	{
		id<CAMetalDrawable> drawable = m_drawable;
		m_drawable = nil;

		// Do not keep the presented back buffer bound, or the layer could not hand it out again.
		if (drawable)
		{
			m_device.ForgetTexture(drawable.texture);

			const FrameCaptureSettings& capture = GetFrameCaptureSettings();
			if (!capture.directory.empty() && m_device.GetCurrentFrame() % capture.interval == 0)
			{
				id<MTLTexture> texture = drawable.texture;
				const uint32_t width = static_cast<uint32_t>(texture.width);
				const uint32_t height = static_cast<uint32_t>(texture.height);
				id<MTLBuffer> readback = [m_device.GetDevice() newBufferWithLength:width * height * 4 options:MTLResourceStorageModeShared];
				id<MTLBlitCommandEncoder> blit = m_device.BeginFrameBlit();
				[blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(width, height, 1)
					toBuffer:readback destinationOffset:0 destinationBytesPerRow:width * 4 destinationBytesPerImage:width * height * 4];
				m_device.FinishAndWait();

				const std::string path = capture.directory + "/frame_" + std::to_string(m_device.GetCurrentFrame()) + ".bmp";
				WriteBmp(path, static_cast<const uint8_t*>([readback contents]), width, height, width * 4);
				ILOG("Captured frame to " << path);
			}
		}

		m_device.PresentFrame(drawable);

		ApplyPendingResize();
	}

	void RenderWindowMetal::SetTitle(const std::string& title)
	{
		m_window.title = [NSString stringWithUTF8String:title.c_str()];
	}

	void RenderWindowMetal::Hide()
	{
		[m_window orderOut:nil];
	}

	void RenderWindowMetal::SetDisplayMode(const bool fullscreenWindow, uint32 monitorIndex, const uint16 width, const uint16 height)
	{
		NSArray<NSScreen*>* screens = [NSScreen screens];
		NSScreen* screen = monitorIndex < screens.count ? screens[monitorIndex] : [NSScreen mainScreen];

		m_fullScreen = fullscreenWindow;
		if (fullscreenWindow)
		{
			[m_window setStyleMask:NSWindowStyleMaskBorderless];
			[m_window setLevel:NSNormalWindowLevel];
			[m_window setFrame:screen.frame display:YES];
			[NSApp setPresentationOptions:NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar];
		}
		else
		{
			[NSApp setPresentationOptions:NSApplicationPresentationDefault];
			[m_window setStyleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable];
			[m_window setLevel:NSNormalWindowLevel];
			[m_window setContentSize:NSMakeSize(width, height)];
			[m_window center];
		}

		[m_window makeKeyAndOrderFront:nil];
		[m_window makeFirstResponder:m_view];
		OnContentResized();
	}

	bool RenderWindowMetal::HasFocus() const
	{
		return [m_window isKeyWindow];
	}

	void RenderWindowMetal::ApplyVSync()
	{
		m_layer.displaySyncEnabled = m_device.IsVSyncEnabled() ? YES : NO;
	}
}
