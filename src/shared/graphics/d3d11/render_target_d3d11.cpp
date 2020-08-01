// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "render_target_d3d11.h"
#include "graphics_device_d3d11.h"


namespace mmo
{
	RenderTargetD3D11::RenderTargetD3D11(GraphicsDeviceD3D11 & device) noexcept
		: m_device(device)
		, m_clearColorFloat{0.0f, 0.0f, 0.0f, 1.0f}
	{
	}

	void RenderTargetD3D11::Activate()
	{
		ID3D11DeviceContext& context = m_device;

		ID3D11RenderTargetView* RenderTargets[1] = { m_renderTargetView.Get() };
		context.OMSetRenderTargets(1, RenderTargets, m_depthStencilView.Get());
	}

	void RenderTargetD3D11::Clear(ClearFlags flags)
	{
		ID3D11DeviceContext& context = m_device;

		// Clear the color buffer?
		const auto converted = static_cast<uint32>(flags);
		if ((converted & static_cast<uint32>(ClearFlags::Color)) != 0)
		{
			// Clear render target and depth stencil view
			context.ClearRenderTargetView(m_renderTargetView.Get(), m_clearColorFloat);
		}

		// Clear depth stencil view
		if ((converted & static_cast<uint32>(ClearFlags::Depth)) != 0)
		{
			context.ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
		}

	}
}
