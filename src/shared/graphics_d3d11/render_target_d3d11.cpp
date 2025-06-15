// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "render_target_d3d11.h"
#include "graphics_device_d3d11.h"


namespace mmo
{
	RenderTargetD3D11::RenderTargetD3D11(GraphicsDeviceD3D11 & device)
		: m_device(device)
		, m_clearColorFloat{0.0f, 0.0f, 0.0f, 0.0f}
	{
	}

	void RenderTargetD3D11::Activate()
	{
		ID3D11DeviceContext& context = m_device;

		if (m_renderTargetView && m_depthStencilView)
		{
			// Color + Depth
			ID3D11RenderTargetView* renderTargets[1] = { m_renderTargetView.Get() };
			context.OMSetRenderTargets(1, renderTargets, m_depthStencilView.Get());
		}
		else if (!m_renderTargetView && m_depthStencilView)
		{
			// Depth only
			context.OMSetRenderTargets(0, nullptr, m_depthStencilView.Get());
		}
		else if (m_renderTargetView && !m_depthStencilView)
		{
			// Color only (no depth)
			ID3D11RenderTargetView* renderTargets[1] = { m_renderTargetView.Get() };
			context.OMSetRenderTargets(1, renderTargets, nullptr);
		}
		else
		{
			// Neither color nor depth is available � invalid usage
			ASSERT(!"RenderTargetD3D11: No valid render or depth target to bind.");
		}
	}

	void RenderTargetD3D11::Clear(ClearFlags flags)
	{
		ID3D11DeviceContext& context = m_device;

		// Clear the color buffer?
		const auto converted = static_cast<uint32>(flags);
		if ((converted & static_cast<uint32>(ClearFlags::Color)) != 0 && m_renderTargetView)
		{			
			// Clear render target and depth stencil view
			context.ClearRenderTargetView(m_renderTargetView.Get(), m_clearColorFloat);
		}

		// Clear depth stencil view
		if ((converted & static_cast<uint32>(ClearFlags::Depth)) != 0 && m_depthStencilView)
		{
			context.ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
		}

	}
}
