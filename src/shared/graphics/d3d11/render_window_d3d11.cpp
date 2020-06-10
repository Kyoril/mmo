// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "render_window_d3d11.h"
#include "graphics_device_d3d11.h"

#include "base/macros.h"

#include <utility>


namespace mmo
{
	/// Name of the render window class.
	static const TCHAR s_d3d11RenderWindowClassName[] = TEXT("D3D11RenderWindow");

	RenderWindowD3D11::RenderWindowD3D11(GraphicsDeviceD3D11 & device, std::string name, uint16 width, uint16 height)
		: RenderWindow(std::move(name), width, height)
		, RenderTargetD3D11(device)
		, m_handle(nullptr)
		, m_pendingWidth(0)
		, m_pendingHeight(0)
		, m_resizePending(false)
	{
		// Create the window handle first
		CreateWindowHandle();

		// Create the swap chain after the window handle has been created
		CreateSwapChain();

		// Lastly, create size dependant resources like the back buffer
		CreateSizeDependantResources();
	}

	RenderWindowD3D11::RenderWindowD3D11(GraphicsDeviceD3D11 & device, std::string name, HWND externalHandle)
		: RenderWindow(std::move(name), 0, 0)
		, RenderTargetD3D11(device)
		, m_handle(externalHandle)
		, m_pendingWidth(0)
		, m_pendingHeight(0)
		, m_resizePending(false)
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
		RenderTargetD3D11::Activate();

		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
	}

	void RenderWindowD3D11::Clear(ClearFlags flags)
	{
		RenderTargetD3D11::Clear(flags);
	}

	void RenderWindowD3D11::Resize(uint16 width, uint16 height)
	{
		ASSERT(width > 0);
		ASSERT(height > 0);

		m_pendingWidth = width;
		m_pendingHeight = height;
		m_resizePending = true;
	}

	void RenderWindowD3D11::Update()
	{
		BOOL IsFullscreenState;
		VERIFY(SUCCEEDED(m_swapChain->GetFullscreenState(&IsFullscreenState, nullptr)));

		const UINT presentFlags = m_device.HasTearingSupport() && !m_device.IsVSyncEnabled() && !IsFullscreenState ? DXGI_PRESENT_ALLOW_TEARING : 0;
		m_swapChain->Present(m_device.IsVSyncEnabled() ? 1 : 0, presentFlags);

		// Apply pending resize
		if (m_resizePending)
		{
			ApplyInternalResize();
			m_resizePending = false;
		}
	}

	void RenderWindowD3D11::SetTitle(const std::string & title)
	{
		::SetWindowTextA(m_handle, title.c_str());
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
			wc.style = CS_OWNDC;

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
		dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
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

		// Resize buffers
		VERIFY(SUCCEEDED(m_swapChain->ResizeBuffers(2, m_width, m_height, DXGI_FORMAT_R8G8B8A8_UNORM, m_device.HasTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0)));

		// Recreate size dependent resources
		CreateSizeDependantResources();

		// No longer resize pending
		m_pendingWidth = 0;
		m_pendingHeight = 0;
	}

	void RenderWindowD3D11::CreateWindowHandle()
	{
		// Window class has to be registered before
		EnsureWindowClassCreated();

		// Prevent double initialization
		ASSERT(m_handle == nullptr);

		// Create the actual window
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
			GetModuleHandle(nullptr), nullptr)));
		m_ownHandle = true;

		// Make the window visible on screen
		ShowWindow(m_handle, SW_SHOWNORMAL);
		UpdateWindow(m_handle);
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
		scd.BufferCount = 2;
		scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scd.BufferDesc.Width = m_width;
		scd.BufferDesc.Height = m_height;
		scd.BufferDesc.RefreshRate.Numerator = 60;	// TODO: Determine monitor refresh rate in Hz and use the value here to
		scd.BufferDesc.RefreshRate.Denominator = 1;	// support high refresh rate monitors
		scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scd.Flags = m_device.HasTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		scd.OutputWindow = m_handle;
		scd.SampleDesc.Count = 1;
		scd.SwapEffect = m_device.HasTearingSupport() ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;
		scd.Windowed = TRUE;
		VERIFY(SUCCEEDED(DXGIFactory->CreateSwapChain(&d3dDev, &scd, &m_swapChain)));
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
