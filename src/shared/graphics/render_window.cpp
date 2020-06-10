
#include "render_window.h"

#include <utility>


namespace mmo
{
	RenderWindow::RenderWindow(std::string name, uint16 width, uint16 height) noexcept
		: RenderTarget(std::move(name), width, height)
	{
	}
}
