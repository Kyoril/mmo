
#pragma once

#include "graphics/render_window.h"

#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace mmo
{
	class GraphicsDeviceD3D11;


	class RenderWindowD3D11 final
		: public RenderWindow
	{
	public:
		RenderWindowD3D11(GraphicsDeviceD3D11& device, std::string name, uint16 width, uint16 height);

	public:
		virtual void Activate() final override;

	private:
		GraphicsDeviceD3D11& m_device;
		HWND m_handle;
		ComPtr<ID3D11RenderTargetView> m_rtv;
	};
}
