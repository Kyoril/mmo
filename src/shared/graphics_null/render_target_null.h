// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/render_target.h"

#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	class GraphicsDeviceNull;

	/// This class is the base class for render targets in D3D11. Note that it does not inherit from
	/// the RenderTarget class of the Graphics library, as this class is an internal base class of 
	/// the d3d11 implementation which capsulates common behavior of d3d11 render targets.
	class RenderTargetNull
	{
	public:
		explicit RenderTargetNull(GraphicsDeviceNull& device) noexcept;
		virtual ~RenderTargetNull() noexcept = default;

	public:
		virtual void Activate();
		virtual void Clear(ClearFlags flags);

	protected:
		GraphicsDeviceNull& m_device;
	};
}