
#pragma once

#include "graphics/graphics_device.h"

// D3D11 header files and namespaces
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace mmo
{
	/// This is the d3d11 implementation of the graphics device class.
	class GraphicsDeviceD3D11
		: public GraphicsDevice
	{
	public:


	private:
		/// The d3d11 device object.
		ComPtr<ID3D11Device> m_device;
		/// The immediate context used to generate the final image.
		ComPtr<ID3D11DeviceContext> m_immContext;
		/// The swap chain.
		ComPtr<IDXGISwapChain> m_swapChain;

	};
}
