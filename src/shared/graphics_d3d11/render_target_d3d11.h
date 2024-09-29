// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "graphics/render_target.h"

#include "base/typedefs.h"

#include <memory>

#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace mmo
{
	class GraphicsDeviceD3D11;

	/// This class is the base class for render targets in D3D11. Note that it does not inherit from
	/// the RenderTarget class of the Graphics library, as this class is an internal base class of 
	/// the d3d11 implementation which capsulates common behavior of d3d11 render targets.
	class RenderTargetD3D11
	{
	public:
		explicit RenderTargetD3D11(GraphicsDeviceD3D11& device) noexcept;
		virtual ~RenderTargetD3D11() noexcept = default;

	public:
		virtual void Activate();
		virtual void Clear(ClearFlags flags);

	protected:
		GraphicsDeviceD3D11& m_device;
		ComPtr<ID3D11RenderTargetView> m_renderTargetView;
		ComPtr<ID3D11DepthStencilView> m_depthStencilView;
		FLOAT m_clearColorFloat[4];
	};
}