
#include "render_window_d3d11.h"
#include "graphics_device_d3d11.h"

#include "base/macros.h"

#include <utility>


namespace mmo
{
	RenderWindowD3D11::RenderWindowD3D11(GraphicsDeviceD3D11 & device, std::string name, uint16 width, uint16 height)
		: RenderWindow(std::move(name), width, height)
		, m_device(device)
	{

	}

	void RenderWindowD3D11::Activate()
	{
		TODO("Implement this method");
	}
}
