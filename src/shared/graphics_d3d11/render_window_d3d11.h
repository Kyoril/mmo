// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/render_window.h"
#include "render_target_d3d11.h"

#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace mmo
{
	class GraphicsDeviceD3D11;


	/// A D3D11 implementation of the render window class. Used for rendering contents in a native WinApi window.
	/// Will support using an external window handle instead of the internal one.
	class RenderWindowD3D11 final
		: public RenderWindow
		, public RenderTargetD3D11
	{
	public:
		RenderWindowD3D11(GraphicsDeviceD3D11& device, std::string name, uint16 width, uint16 height, bool fullScreen);
		RenderWindowD3D11(GraphicsDeviceD3D11& device, std::string name, HWND externalHandle);

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

	private:
		/// Ensures that the internal window class is created.
		void EnsureWindowClassCreated();
		/// Creates an internal render window.
		void CreateWindowHandle();
		/// Creates a swap chain that is used to swap the back buffer with the front buffer and make rendering visible on screen.
		void CreateSwapChain();
		/// Creates resources that might be recreated if the buffer sizes change.
		void CreateSizeDependantResources();
		/// 
		void ApplyInternalResize();

	private:
		/// The render window callback procedure for internally created windows.
		static LRESULT RenderWindowProc(HWND Wnd, UINT Msg, WPARAM WParam, LPARAM LParam);

	private:
		/// The native window handle that is being used.
		HWND m_handle;
		/// Whether this is our own handle that we need to destroy ourself.
		bool m_ownHandle;
		ComPtr<IDXGISwapChain> m_swapChain;
		/// Pending width after resize.
		uint16 m_pendingWidth;
		/// Pending height after resize.
		uint16 m_pendingHeight;
		/// 
		bool m_resizePending;

		bool m_fullScreen;

		bool m_prevFullScreenState = false;
	};
}
