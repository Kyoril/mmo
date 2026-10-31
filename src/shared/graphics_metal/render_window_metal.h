// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/render_window.h"
#include "graphics_device_metal.h"

@class NSWindow;
@class MMOMetalView;
@class MMOWindowDelegate;

namespace mmo
{
	/// @brief A Cocoa window whose content view is backed by a CAMetalLayer.
	/// @remarks Sizes are in points: the layer renders at one pixel per point and the window server
	///          scales it on Retina screens, so the engine's pixel based UI keeps its size.
	class RenderWindowMetal final
		: public RenderWindow
	{
	public:
		RenderWindowMetal(GraphicsDeviceMetal& device, std::string name, uint16 width, uint16 height, bool fullScreen);

		~RenderWindowMetal() override;

	public:
		// ~ Begin RenderTarget
		void Activate() override;

		void ApplyPendingResize() override;

		void Clear(ClearFlags flags) override;

		void Resize(uint16 width, uint16 height) override;

		/// Presents the frame.
		void Update() override;
		// ~ End RenderTarget

		// ~ Begin RenderWindow
		void SetTitle(const std::string& title) override;

		void Hide() override;

		void SetDisplayMode(bool fullscreenWindow, uint32 monitorIndex, uint16 width, uint16 height) override;

		[[nodiscard]] bool HasFocus() const override;
		// ~ End RenderWindow

	public:
		/// Applies the device's vertical sync setting to the layer.
		void ApplyVSync();

		/// Called by the window delegate when the content view changed its size.
		void OnContentResized();

		/// Called by the window delegate when the user wants to close the window.
		void OnCloseRequested();

		[[nodiscard]] NSWindow* GetNativeWindow() const { return m_window; }

	private:
		id<MTLTexture> AcquireBackBuffer();

		void CreateDepthBuffer();

	private:
		GraphicsDeviceMetal& m_device;
		NSWindow* m_window = nil;
		MMOMetalView* m_view = nil;
		MMOWindowDelegate* m_delegate = nil;
		CAMetalLayer* m_layer = nil;
		id<CAMetalDrawable> m_drawable = nil;
		id<MTLTexture> m_depthTexture = nil;
		bool m_resizePending = false;
		uint16 m_pendingWidth = 0;
		uint16 m_pendingHeight = 0;
		bool m_fullScreen = false;
	};
}
