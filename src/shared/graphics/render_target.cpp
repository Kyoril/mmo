#include "render_target.h"

#include <utility>

namespace mmo
{
	RenderTarget::RenderTarget(std::string name, uint16 width, uint16 height) noexcept
		: m_name(std::move(name))
		, m_width(width)
		, m_height(height)
	{
	}

}