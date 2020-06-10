
#pragma once

#include "render_target.h"

namespace mmo
{
	/// 
	class RenderWindow
		: public RenderTarget
	{
	public:
		RenderWindow(std::string name, uint16 width, uint16 height) noexcept;
		virtual ~RenderWindow() noexcept = default;
	};

	typedef std::shared_ptr<RenderWindow> RenderWindowPtr;
}
