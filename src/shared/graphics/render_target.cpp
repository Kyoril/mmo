// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "render_target.h"
#include "graphics_device.h"

#include <utility>

namespace mmo
{
	RenderTarget::RenderTarget(std::string name, uint16 width, uint16 height) noexcept
		: m_name(std::move(name))
		, m_width(width)
		, m_height(height)
	{
	}

	void RenderTarget::Activate()
	{
		GraphicsDevice::Get().RenderTargetActivated(shared_from_this());
	}

	MultiRenderTarget::MultiRenderTarget(std::string name, const uint16 width, const uint16 height, PixelFormat format) noexcept
		: RenderTarget(std::move(name), width, height)
	{
	}

	bool MultiRenderTarget::AddRenderTarget(const RenderTargetPtr& renderTarget)
	{
		// TODO: Get max render targets from graphics device caps as there is a hardware limit to it!
		if (m_renderTargets.size() >= 8)
		{
			return false;
		}

		// Render target already added?
		const auto it = std::find_if(m_renderTargets.begin(), m_renderTargets.end(), [renderTarget](const RenderTargetPtr& rt) { return rt == renderTarget; });
		if (it != m_renderTargets.end())
		{
			return false;
		}

		// Render target must be of the same size!
		if (renderTarget->GetWidth() != m_width || renderTarget->GetHeight() != m_height)
		{
			return false;
		}

		// Ensure render target is not an MRT itself
		if (std::dynamic_pointer_cast<MultiRenderTarget>(renderTarget))
		{
			return false;
		}

		// Add render target to the list
		m_renderTargets.push_back(renderTarget);
		return true;
	}

	void MultiRenderTarget::RemoveRenderTarget(const size_t index)
	{
		ASSERT(index < m_renderTargets.size());

		auto it = m_renderTargets.begin();
		if (index > 0) std::advance(it, index);

		m_renderTargets.erase(it);
	}
}
