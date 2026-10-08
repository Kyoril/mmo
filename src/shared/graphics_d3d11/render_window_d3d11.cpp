// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "render_window_d3d11.h"

#include <dwmapi.h>

#include "graphics_device_d3d11.h"
#include "display_monitors_d3d11.h"

#include "base/macros.h"

#include <algorithm>
#include <utility>

#include "log/default_log_levels.h"

#pragma comment(lib, "Dwmapi.lib")


namespace mmo
{
	/// Name of the render window class.
	static const TCHAR s_d3d11RenderWindowClassName[] = TEXT("D3D11RenderWindow");

	RenderWindowD3D11::RenderWindowD3D11(GraphicsDeviceD3D11 & device, std::string name, uint16 width, uint16 height, bool fullScreen, uint32 monitorIndex)
		: RenderWindow(std::move(name), width, height)
		, RenderTargetD3D11(device)
		, m_handle(nullptr)
		, m_pendingWidth(0)
		, m_pendingHeight(0)
		, m_resizePending(false)
		, m_fullScreen(false)
	{
		// Create the window handle first
		CreateWindowHandle();

		// Create the swap chain after the window handle has been created
		CreateSwapChain();

		// Lastly, create size dependant resources like the back buffer
		CreateSizeDependantResources();

		// Places the still hidden window on its monitor and shows it. The swap chain follows through
		// the resulting WM_SIZE like after any other resize.
		SetDisplayMode(fullScreen, monitorIndex, width, height);
	}

	RenderWindowD3D11::RenderWindowD3D11(GraphicsDeviceD3D11 & device, std::string name, HWND externalHandle)
		: RenderWindow(std::move(name), 0, 0)
		, RenderTargetD3D11(device)
		, m_handle(externalHandle)
		, m_pendingWidth(0)
		, m_pendingHeight(0)
		, m_resizePending(false)
		, m_fullScreen(false)
	{
		ASSERT(externalHandle);
		
		// Use the window handle
		m_handle = externalHandle;
		m_ownHandle = false;

		// Determine size of the window
		RECT cr;
		VERIFY(GetClientRect(m_handle, &cr));
		m_width = cr.right - cr.left;
		m_height = cr.bottom - cr.top;

		// VERIFY size
		ASSERT(m_width > 0);
		ASSERT(m_height > 0);

		// Create the swap chain after the window handle has been created
		CreateSwapChain();

		// Lastly, create size dependant resources like the back buffer
		CreateSizeDependantResources();
	}

	void RenderWindowD3D11::Activate()
	{
		RenderTarget::Activate();
		RenderTargetD3D11::Activate();

		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
	}

	void RenderWindowD3D11::ApplyPendingResize()
	{
		if (m_resizePending)
		{
			ApplyInternalResize();
			m_resizePending = false;
		}
	}

	void RenderWindowD3D11::Clear(ClearFlags flags)
	{
		RenderTargetD3D11::Clear(flags);
	}

	void RenderWindowD3D11::Resize(uint16 width, uint16 height)
	{
		if (width == 0) return;
		if (height == 0) return;
		if (width == m_width && height == m_height) return;

		m_pendingWidth = width;
		m_pendingHeight = height;
		m_resizePending = true;
	}

	void RenderWindowD3D11::Update()
	{
		// Unbind render target before present
		ID3D11DeviceContext& d3dCtx = m_device;
		d3dCtx.OMSetRenderTargets(0, nullptr, nullptr);

		// Latch the per-frame draw-call counter at the true frame boundary (see GetBatchCount).
		m_device.LatchFrameBatchCount();

		const UINT presentFlags = m_device.HasTearingSupport() && !m_device.IsVSyncEnabled() ? DXGI_PRESENT_ALLOW_TEARING : 0;
		m_swapChain->Present(m_device.IsVSyncEnabled() ? 1 : 0, presentFlags);

		// Apply pending resize
		ApplyPendingResize();
	}

	void RenderWindowD3D11::SetTitle(const std::string & title)
	{
		::SetWindowTextA(m_handle, title.c_str());
	}

	void RenderWindowD3D11::Hide()
	{
		// The window never owns the display exclusively, so hiding it is enough to make room for
		// UI shown by someone else, such as a message box.
		if (m_handle)
		{
			::ShowWindow(m_handle, SW_HIDE);
		}
	}

	void RenderWindowD3D11::SetDisplayMode(const bool fullscreenWindow, const uint32 monitorIndex, const uint16 width, const uint16 height)
	{
		// A window handed in from outside (the editor viewport) is laid out by its owner.
		if (!m_ownHandle || !m_handle)
		{
			return;
		}

		Win32Monitor monitor;
		if (!FindWin32Monitor(monitorIndex, monitor))
		{
			WLOG("No monitor found to place the game window on");
			return;
		}

		// A maximized or minimized window ignores the position and size given below.
		if (::IsZoomed(m_handle) || ::IsIconic(m_handle))
		{
			::ShowWindow(m_handle, SW_RESTORE);
		}

		constexpr UINT flags = SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER;
		if (fullscreenWindow)
		{
			::SetWindowLongPtr(m_handle, GWL_STYLE, WS_POPUP | WS_VISIBLE);

			const RECT& bounds = monitor.bounds;
			::SetWindowPos(m_handle, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, flags);
		}
		else
		{
			constexpr DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
			::SetWindowLongPtr(m_handle, GWL_STYLE, style);

			const LONG clientWidth = std::max<LONG>(320, width);
			const LONG clientHeight = std::max<LONG>(240, height);

			RECT outer = { 0, 0, clientWidth, clientHeight };
			::AdjustWindowRect(&outer, style, FALSE);
			LONG outerWidth = outer.right - outer.left;
			LONG outerHeight = outer.bottom - outer.top;

			// Centered on the work area, but never with the title bar above or left of it, so the
			// window can still be grabbed when it is as large as the monitor.
			const RECT& work = monitor.workArea;
			const LONG x = std::max(work.left, work.left + ((work.right - work.left) - outerWidth) / 2);
			const LONG y = std::max(work.top, work.top + ((work.bottom - work.top) - outerHeight) / 2);
			::SetWindowPos(m_handle, HWND_TOP, x, y, outerWidth, outerHeight, flags);

			// AdjustWindowRect does not know the frame size at the monitor's DPI, so correct the
			// outer size by whatever the client area missed.
			RECT client;
			if (::GetClientRect(m_handle, &client) &&
				(client.right != clientWidth || client.bottom != clientHeight))
			{
				outerWidth += clientWidth - client.right;
				outerHeight += clientHeight - client.bottom;
				::SetWindowPos(m_handle, HWND_TOP, x, y, outerWidth, outerHeight, SWP_NOOWNERZORDER);
			}
		}

		m_fullScreen = fullscreenWindow;
	}

	bool RenderWindowD3D11::HasFocus() const
	{
		if (!m_ownHandle)
		{
			return true;
		}

		return m_handle && ::GetForegroundWindow() == m_handle && !::IsIconic(m_handle);
	}

	void RenderWindowD3D11::EnsureWindowClassCreated()
	{
		static bool s_windowClassCreated = false;
		if (!s_windowClassCreated)
		{
			// Create the window class structure
			WNDCLASSEX wc;
			ZeroMemory(&wc, sizeof(wc));

			// Prepare struct
			wc.cbSize = sizeof(wc);
			wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
			wc.hInstance = GetModuleHandle(nullptr);
			wc.lpfnWndProc = &RenderWindowD3D11::RenderWindowProc;
			wc.lpszClassName = s_d3d11RenderWindowClassName;
			wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;

			VERIFY(RegisterClassEx(&wc) >= 0);
			s_windowClassCreated = true;
		}
	}

	void RenderWindowD3D11::CreateSizeDependantResources()
	{
		ASSERT(m_swapChain);

		// Grab the d3d11 device
		ID3D11Device& d3dDev = m_device;

		// Create the render target view
		ComPtr<ID3D11Texture2D> renderTargetBuffer;
		VERIFY(SUCCEEDED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&renderTargetBuffer))));

		// Create the render target view
		VERIFY(SUCCEEDED(d3dDev.CreateRenderTargetView(renderTargetBuffer.Get(), nullptr, &m_renderTargetView)));

		// Create a depth buffer
		D3D11_TEXTURE2D_DESC texd;
		ZeroMemory(&texd, sizeof(texd));
		texd.Width = m_width;
		texd.Height = m_height;
		texd.ArraySize = 1;
		texd.MipLevels = 1;
		texd.SampleDesc.Count = 1;
		texd.Format = DXGI_FORMAT_D32_FLOAT;
		texd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		ComPtr<ID3D11Texture2D> depthBuffer;
		VERIFY(SUCCEEDED(d3dDev.CreateTexture2D(&texd, nullptr, &depthBuffer)));

		// Create the depth stencil view
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvd;
		ZeroMemory(&dsvd, sizeof(dsvd));
		dsvd.Format = DXGI_FORMAT_D32_FLOAT;
		dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

		VERIFY(SUCCEEDED(d3dDev.CreateDepthStencilView(depthBuffer.Get(), &dsvd, &m_depthStencilView)));
	}

	void RenderWindowD3D11::ApplyInternalResize()
	{
		// Reset buffer references
		m_depthStencilView.Reset();
		m_renderTargetView.Reset();

		// Apply size values
		m_width = m_pendingWidth;
		m_height = m_pendingHeight;

		// Resize buffers (keep the triple-buffered count from CreateSwapChain — passing 0 preserves
		// the existing buffer count, but we pass 3 explicitly to match and stay robust to changes).
		VERIFY(SUCCEEDED(m_swapChain->ResizeBuffers(3, m_width, m_height, DXGI_FORMAT_R8G8B8A8_UNORM, m_device.HasTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0)));

		// Recreate size dependent resources
		CreateSizeDependantResources();

		// Hacky way to reset the viewport size
		float minZ, maxZ;
		m_device.GetViewport(nullptr, nullptr, nullptr, nullptr, &minZ, &maxZ);
		m_device.SetViewport(0, 0, m_width, m_height, minZ, maxZ);

		// No longer resize pending
		m_pendingWidth = 0;
		m_pendingHeight = 0;

		// Notify observers
		Resized(m_width, m_height);
	}

	void RenderWindowD3D11::CreateWindowHandle()
	{
		// Window class has to be registered before
		EnsureWindowClassCreated();

		// Prevent double initialization
		ASSERT(m_handle == nullptr);
		
		// Created as a regular window; SetDisplayMode switches the style for a fullscreen window. DXGI
		// never takes the window fullscreen itself, so it does not fight us over the styles.
		const DWORD ws = WS_OVERLAPPEDWINDOW;

		// Calculate the real window size needed to make the client area the requestes size
		RECT r = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
		AdjustWindowRect(&r, ws, FALSE);

		const UINT sx = GetSystemMetrics(SM_CXSCREEN);
		const UINT sy = GetSystemMetrics(SM_CYSCREEN);
		const UINT x = sx / 2 - (r.right - r.left) / 2;
		const UINT y = sy / 2 - (r.bottom - r.top) / 2;

		// Create the actual window
		VERIFY((m_handle = CreateWindowEx(0, s_d3d11RenderWindowClassName, TEXT("D3D11 Render Window"),
			ws, x, y, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
			GetModuleHandle(nullptr), this)));
		m_ownHandle = true;

		// Enable Mica
		DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW; // 2 = Mica (on Windows 11)
		DwmSetWindowAttribute(m_handle, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

		BOOL supportDarkMode = TRUE;
		DwmSetWindowAttribute(m_handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &supportDarkMode, sizeof(supportDarkMode));

		DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
		DwmSetWindowAttribute(m_handle, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

		// Not shown yet: SetDisplayMode places the window on its monitor first, so it never flashes up
		// at a default position.
	}

	void RenderWindowD3D11::CreateSwapChain()
	{
		ASSERT(!m_swapChain);

		// Grab the d3d11 device
		ID3D11Device& d3dDev = m_device;

		// Grab the dxgi device object
		ComPtr<IDXGIDevice> DXGIDevice;
		VERIFY(SUCCEEDED(d3dDev.QueryInterface(__uuidof(IDXGIDevice), &DXGIDevice)));

		// Query the adapter that created the device
		ComPtr<IDXGIAdapter> DXGIAdapter;
		VERIFY(SUCCEEDED(DXGIDevice->GetAdapter(&DXGIAdapter)));

		// Now query the factory that created the adapter object
		ComPtr<IDXGIFactory5> DXGIFactory;
		VERIFY(SUCCEEDED(DXGIAdapter->GetParent(IID_PPV_ARGS(&DXGIFactory))));

		// We now can create a swap chain using the factory
		DXGI_SWAP_CHAIN_DESC scd;
		ZeroMemory(&scd, sizeof(scd));
		// Triple-buffer so the GPU can render ahead. With the flip model and VSync enabled, a
		// 2-buffer chain drops straight to 30fps the moment a 60Hz frame is missed (no room to work
		// ahead); a third buffer lets frames queue, softening that hard half-rate cliff.
		scd.BufferCount = 3;
		scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scd.BufferDesc.Width = m_width;
		scd.BufferDesc.Height = m_height;
		scd.BufferDesc.RefreshRate.Numerator = 60;
		scd.BufferDesc.RefreshRate.Denominator = 1;
		scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scd.Flags = m_device.HasTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		scd.OutputWindow = m_handle;
		scd.SampleDesc.Count = 1;
		scd.SwapEffect = m_device.HasTearingSupport() ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;
		// Always windowed: a fullscreen window is a borderless window covering the monitor. With the flip
		// model the compositor scans it out directly, which is as fast as exclusive fullscreen but switches
		// monitors, alt-tabs and overlays without display mode changes.
		scd.Windowed = TRUE;

		VERIFY(SUCCEEDED(DXGIFactory->CreateSwapChain(&d3dDev, &scd, &m_swapChain)));

		// Keep DXGI from switching to exclusive fullscreen on Alt+Enter behind our back.
		DXGIFactory->MakeWindowAssociation(m_handle, DXGI_MWA_NO_ALT_ENTER);
	}

	LRESULT RenderWindowD3D11::RenderWindowProc(HWND Wnd, UINT Msg, WPARAM WParam, LPARAM LParam)
	{
		RenderWindowD3D11* window = reinterpret_cast<RenderWindowD3D11*>(GetWindowLongPtr(Wnd, GWLP_USERDATA));

		switch (Msg)
		{
		case WM_CREATE:
			{
				// Set the pointer to the current window instance as user parameter for the created window.
				// This has to be done in this message as prior to this message, the window doesn't exist.
				LPCREATESTRUCT cs = reinterpret_cast<LPCREATESTRUCT>(LParam);
				SetWindowLongPtr(Wnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
			}
			return 0;
		case WM_SETCURSOR:
			// Check if the cursor is over the client area
			if (LOWORD(LParam) == HTCLIENT) 
			{
				// Set your custom cursor
				HCURSOR customCursor = static_cast<HCURSOR>(GraphicsDevice::Get().GetHardwareCursor());
				if (customCursor)
				{
					SetCursor(customCursor);
					return TRUE;  // Mark the message as handled
				}
				
				return FALSE;
			}
			break;
		case WM_DESTROY:
			if (window) window->Closed();
			return 0;
		case WM_SIZE:
			if (window) window->Resize(LOWORD(LParam), HIWORD(LParam));
			return 0;
		}

		return DefWindowProc(Wnd, Msg, WParam, LParam);
	}
}
