// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "graphics_device_d3d11.h"
#include "vertex_buffer_d3d11.h"
#include "index_buffer_d3d11.h"
#include "vertex_shader_d3d11.h"
#include "pixel_shader_d3d11.h"
#include "texture_d3d11.h"
#include "render_texture_d3d11.h"
#include "render_window_d3d11.h"

#include "base/macros.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")


namespace mmo
{
	void GraphicsDeviceD3D11::CheckTearingSupport()
	{
		// Rather than create the 1.5 factory interface directly, we create the 1.4
		// interface and query for the 1.5 interface. This will enable the graphics
		// debugging tools which might not support the 1.5 factory interface.

		ComPtr<IDXGIFactory4> factory4;
		HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory4));

		BOOL allowTearing = FALSE;
		if (SUCCEEDED(hr))
		{
			ComPtr<IDXGIFactory5> factory5;
			hr = factory4.As(&factory5);
			if (SUCCEEDED(hr))
			{
				hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
			}
		}

		m_tearingSupport = SUCCEEDED(hr) && allowTearing;
	}

	void GraphicsDeviceD3D11::CreateD3D11()
	{
		// Check for variable refresh rate display support on the platform
		CheckTearingSupport();

		// A list of supported feature levels.
		const D3D_FEATURE_LEVEL supportedFeatureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1 };

		// Setup device flags
		UINT deviceCreationFlags = 0;

#if defined(_DEBUG)
		// If the project is in a debug build, enable the debug layer.
		deviceCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		// Create the device object
		VERIFY(SUCCEEDED(D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			deviceCreationFlags,
			supportedFeatureLevels,
			ARRAYSIZE(supportedFeatureLevels),
			D3D11_SDK_VERSION,
			&m_device,
			&m_featureLevel,
			&m_immContext)));

		// Create input layouts
		CreateInputLayouts();

		// Create blend states
		CreateBlendStates();

		// Setup constant buffers
		CreateConstantBuffers();

		// Create rasterizer state
		CreateRasterizerStates();

		// Create sampler state
		CreateSamplerStates();

		// Setup depth states
		CreateDepthStates();
	}

	void GraphicsDeviceD3D11::CreateInputLayouts()
	{
		// Include shader code
		#include "shaders/VS_Pos.h"
		#include "shaders/VS_PosColor.h"
		#include "shaders/VS_PosColorNormal.h"
		#include "shaders/VS_PosColorNormalTex.h"
		#include "shaders/VS_PosColorTex.h"

		// Setup vertex shaders
		VertexShaders[VertexFormat::Pos] = CreateShader(ShaderType::VertexShader, g_VS_Pos, ARRAYSIZE(g_VS_Pos));
		VertexShaders[VertexFormat::PosColor] = CreateShader(ShaderType::VertexShader, g_VS_PosColor, ARRAYSIZE(g_VS_PosColor));
		VertexShaders[VertexFormat::PosColorNormal] = CreateShader(ShaderType::VertexShader, g_VS_PosColorNormal, ARRAYSIZE(g_VS_PosColorNormal));
		VertexShaders[VertexFormat::PosColorNormalTex1] = CreateShader(ShaderType::VertexShader, g_VS_PosColorNormalTex, ARRAYSIZE(g_VS_PosColorNormalTex));
		VertexShaders[VertexFormat::PosColorTex1] = CreateShader(ShaderType::VertexShader, g_VS_PosColorTex, ARRAYSIZE(g_VS_PosColorTex));

		#include "shaders/PS_Pos.h"
		#include "shaders/PS_PosColor.h"
		#include "shaders/PS_PosColorNormal.h"
		#include "shaders/PS_PosColorNormalTex.h"
		#include "shaders/PS_PosColorTex.h"

		// Setup pixel shaders
		PixelShaders[VertexFormat::Pos] = CreateShader(ShaderType::PixelShader, g_PS_Pos, ARRAYSIZE(g_PS_Pos));
		PixelShaders[VertexFormat::PosColor] = CreateShader(ShaderType::PixelShader, g_PS_PosColor, ARRAYSIZE(g_PS_PosColor));
		PixelShaders[VertexFormat::PosColorNormal] = CreateShader(ShaderType::PixelShader, g_PS_PosColorNormal, ARRAYSIZE(g_PS_PosColorNormal));
		PixelShaders[VertexFormat::PosColorNormalTex1] = CreateShader(ShaderType::PixelShader, g_PS_PosColorNormalTex, ARRAYSIZE(g_PS_PosColorNormalTex));
		PixelShaders[VertexFormat::PosColorTex1] = CreateShader(ShaderType::PixelShader, g_PS_PosColorTex, ARRAYSIZE(g_PS_PosColorTex));

		ComPtr<ID3D11InputLayout> InputLayout;

		// EGxVertexFormat::Pos
		const D3D11_INPUT_ELEMENT_DESC PosElements[] = {
			{ "SV_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		VERIFY(SUCCEEDED(m_device->CreateInputLayout(PosElements, ARRAYSIZE(PosElements), g_VS_Pos, ARRAYSIZE(g_VS_Pos), &InputLayout)));
		InputLayouts[VertexFormat::Pos] = InputLayout;

		// EGxVertexFormat::PosColor
		const D3D11_INPUT_ELEMENT_DESC PosColElements[] = {
			{ "SV_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		VERIFY(SUCCEEDED(m_device->CreateInputLayout(PosColElements, ARRAYSIZE(PosColElements), g_VS_PosColor, ARRAYSIZE(g_VS_PosColor), &InputLayout)));
		InputLayouts[VertexFormat::PosColor] = InputLayout;

		// EGxVertexFormat::PosColorNormal
		const D3D11_INPUT_ELEMENT_DESC PosColNormElements[] = {
			{ "SV_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		VERIFY(SUCCEEDED(m_device->CreateInputLayout(PosColNormElements, ARRAYSIZE(PosColNormElements), g_VS_PosColorNormal, ARRAYSIZE(g_VS_PosColorNormal), &InputLayout)));
		InputLayouts[VertexFormat::PosColorNormal] = InputLayout;

		// EGxVertexFormat::PosColorNormalTex1
		const D3D11_INPUT_ELEMENT_DESC PosColNormTexElements[] = {
			{ "SV_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		VERIFY(SUCCEEDED(m_device->CreateInputLayout(PosColNormTexElements, ARRAYSIZE(PosColNormTexElements), g_VS_PosColorNormalTex, ARRAYSIZE(g_VS_PosColorNormalTex), &InputLayout)));
		InputLayouts[VertexFormat::PosColorNormalTex1] = InputLayout;

		// EGxVertexFormat::PosColorTex1
		const D3D11_INPUT_ELEMENT_DESC PosColTexElements[] = {
			{ "SV_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		VERIFY(SUCCEEDED(m_device->CreateInputLayout(PosColTexElements, ARRAYSIZE(PosColTexElements), g_VS_PosColorTex, ARRAYSIZE(g_VS_PosColorTex), &InputLayout)));
		InputLayouts[VertexFormat::PosColorTex1] = InputLayout;
	}

	void GraphicsDeviceD3D11::CreateBlendStates()
	{
		D3D11_BLEND_DESC bd;
		ZeroMemory(&bd, sizeof(bd));
		bd.RenderTarget[0].BlendEnable = true;
		bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
		bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		VERIFY(SUCCEEDED(m_device->CreateBlendState(&bd, &m_opaqueBlendState)));

		// Create the blend state
		ZeroMemory(&bd, sizeof(bd));
		bd.RenderTarget[0].BlendEnable = true;
		bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		VERIFY(SUCCEEDED(m_device->CreateBlendState(&bd, &m_alphaBlendState)));
	}

	void GraphicsDeviceD3D11::CreateConstantBuffers()
	{
		// Fill matrix array with identity matrices
		m_transform[0] = Matrix4::Identity;
		m_transform[1] = Matrix4::Identity;
		m_transform[2] = Matrix4::Identity;

		D3D11_BUFFER_DESC cbd;
		ZeroMemory(&cbd, sizeof(cbd));
		cbd.Usage = D3D11_USAGE_DEFAULT;
		cbd.ByteWidth = sizeof(Matrix4) * 3;
		cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		D3D11_SUBRESOURCE_DATA sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.pSysMem = &m_transform;

		VERIFY(SUCCEEDED(m_device->CreateBuffer(&cbd, nullptr, &m_matrixBuffer)));
	}

	void GraphicsDeviceD3D11::CreateRasterizerStates()
	{
		// Create the default rasterizer state
		D3D11_RASTERIZER_DESC rd;
		ZeroMemory(&rd, sizeof(rd));
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_NONE;
		VERIFY(SUCCEEDED(m_device->CreateRasterizerState(&rd, &m_defaultRasterizerState)));

		// Create the rasterizer state with support for scissor rects
		rd.ScissorEnable = TRUE;
		VERIFY(SUCCEEDED(m_device->CreateRasterizerState(&rd, &m_scissorRasterizerState)));
		
	}

	void GraphicsDeviceD3D11::CreateSamplerStates()
	{
		D3D11_SAMPLER_DESC sd;
		ZeroMemory(&sd, sizeof(sd));

		sd.Filter = D3D11_FILTER_ANISOTROPIC;
		sd.MaxAnisotropy = D3D11_MAX_MAXANISOTROPY;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

		VERIFY(SUCCEEDED(m_device->CreateSamplerState(&sd, &m_samplerState)));
	}

	void GraphicsDeviceD3D11::CreateDepthStates()
	{
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
		depthStencilDesc.DepthEnable = TRUE;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		depthStencilDesc.StencilEnable = FALSE;
		depthStencilDesc.StencilReadMask = 0xFF;
		depthStencilDesc.StencilWriteMask = 0xFF;

		// Stencil operations if pixel is front-facing
		depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
		depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		// Stencil operations if pixel is back-facing
		depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
		depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		VERIFY( SUCCEEDED(m_device->CreateDepthStencilState(&depthStencilDesc, m_depthStencilState.GetAddressOf())));
	}

	void GraphicsDeviceD3D11::Reset()
	{
		// Clear the state
		m_immContext->ClearState();

		// Set rasterizer state
		m_immContext->RSSetState(m_defaultRasterizerState.Get());
		m_immContext->OMSetDepthStencilState(m_depthStencilState.Get(), 0);

		// Update the constant buffer
		if (m_matrixDirty)
		{
			// Reset transforms
			m_transform[0] = Matrix4::Identity;
			m_transform[1] = Matrix4::Identity;
			m_transform[2] = Matrix4::Identity;
			m_immContext->UpdateSubresource(m_matrixBuffer.Get(), 0, 0, &m_transform, 0, 0);
			m_matrixDirty = false;
		}

		// Set the constant buffers
		ID3D11Buffer* Buffers[] = { m_matrixBuffer.Get() };
		m_immContext->VSSetConstantBuffers(0, 1, Buffers);

		// Default blend state
		m_immContext->OMSetBlendState(m_opaqueBlendState.Get(), nullptr, 0xffffffff);

		// Warning: By default we have no active render target nor any viewport set. This needs to be done afterwards
	}

	void GraphicsDeviceD3D11::SetClearColor(uint32 clearColor)
	{
		// Set clear color value in base device
		GraphicsDevice::SetClearColor(clearColor);

		// Calculate d3d11 float clear color values
		BYTE r = GetRValue(clearColor);
		BYTE g = GetGValue(clearColor);
		BYTE b = GetBValue(clearColor);
		m_clearColorFloat[0] = (float)r / 255.0f;
		m_clearColorFloat[1] = (float)g / 255.0f;
		m_clearColorFloat[2] = (float)b / 255.0f;
	}

	void GraphicsDeviceD3D11::Create(const GraphicsDeviceDesc& desc)
	{
		// Create the device
		GraphicsDevice::Create(desc);

		// Initialize Direct3D
		CreateD3D11();

		// Create an automatic render window if requested
		if (desc.customWindowHandle == nullptr)
		{
			m_autoCreatedWindow = CreateRenderWindow("__auto_window__", desc.width, desc.height);
		}
		else
		{
			m_autoCreatedWindow = std::make_shared<RenderWindowD3D11>(*this, "__auto_window__", (HWND)desc.customWindowHandle);
		}
	}

	void GraphicsDeviceD3D11::Clear(ClearFlags Flags)
	{
		// Reset the current state
		Reset();

		// Set the current render target
		m_autoCreatedWindow->Activate();
		m_autoCreatedWindow->Clear(Flags);
	}

	VertexBufferPtr GraphicsDeviceD3D11::CreateVertexBuffer(size_t VertexCount, size_t VertexSize, bool dynamic, const void * InitialData)
	{
		return std::make_unique<VertexBufferD3D11>(*this, VertexCount, VertexSize, dynamic, InitialData);
	}

	IndexBufferPtr GraphicsDeviceD3D11::CreateIndexBuffer(size_t IndexCount, IndexBufferSize IndexSize, const void * InitialData)
	{
		return std::make_unique<IndexBufferD3D11>(*this, IndexCount, IndexSize, InitialData);
	}

	ShaderPtr GraphicsDeviceD3D11::CreateShader(ShaderType Type, const void * ShaderCode, size_t ShaderCodeSize)
	{
		switch (Type)
		{
		case ShaderType::VertexShader:
			return std::make_unique<VertexShaderD3D11>(*this, ShaderCode, ShaderCodeSize);
		case ShaderType::PixelShader:
			return std::make_unique<PixelShaderD3D11>(*this, ShaderCode, ShaderCodeSize);
		default:
			ASSERT(! "This shader type can't yet be created - implement it for D3D11!");
		}

		return nullptr;
	}

	void GraphicsDeviceD3D11::Draw(uint32 vertexCount, uint32 start)
	{
		// Update the constant buffer
		m_matrixDirty = false;
		m_immContext->UpdateSubresource(m_matrixBuffer.Get(), 0, 0, &m_transform, 0, 0);

		// Execute draw command
		m_immContext->Draw(vertexCount, start);
	}

	void GraphicsDeviceD3D11::DrawIndexed()
	{
		// Update the constant buffer
		m_matrixDirty = false;
		m_immContext->UpdateSubresource(m_matrixBuffer.Get(), 0, 0, &m_transform, 0, 0);

		// Execute draw command
		m_immContext->DrawIndexed(m_indexCount, 0, 0);
	}

	namespace
	{
		/// Determines the D3D11_PRIMITIVE_TOPOLOGY value from an EGxTopologyType value.
		static inline D3D11_PRIMITIVE_TOPOLOGY D3DTopologyType(TopologyType Type)
		{
			switch (Type)
			{
			case TopologyType::PointList:
				return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
			case TopologyType::LineList:
				return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
			case TopologyType::LineStrip:
				return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
			case TopologyType::TriangleList:
				return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			case TopologyType::TriangleStrip:
				return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
			default:
				ASSERT(false);
				return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
			}
		}
	}

	void GraphicsDeviceD3D11::SetTopologyType(TopologyType InType)
	{
		const D3D11_PRIMITIVE_TOPOLOGY Topology = D3DTopologyType(InType);
		ASSERT(Topology != D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);

		m_immContext->IASetPrimitiveTopology(Topology);
	}

	void GraphicsDeviceD3D11::SetVertexFormat(VertexFormat InFormat)
	{
		auto it = InputLayouts.find(InFormat);
		ASSERT(it != InputLayouts.end());

		m_immContext->IASetInputLayout(it->second.Get());

		auto vertIt = VertexShaders.find(InFormat);
		if (vertIt != VertexShaders.end())
		{
			vertIt->second->Set();
		}

		auto pixIt = PixelShaders.find(InFormat);
		if (pixIt != PixelShaders.end())
		{
			pixIt->second->Set();

			// Set sampler state
			ID3D11SamplerState* const samplerStates = m_samplerState.Get();
			m_immContext->PSSetSamplers(0, 1, &samplerStates);
		}
	}

	void GraphicsDeviceD3D11::SetBlendMode(BlendMode InBlendMode)
	{
		ID3D11BlendState* blendState = nullptr;

		// Set blend state
		switch (InBlendMode)
		{
		case BlendMode::Opaque:
			blendState = m_opaqueBlendState.Get();
			break;
		case BlendMode::Alpha:
			blendState = m_alphaBlendState.Get();
			break;
		}

		ASSERT(blendState);
		m_immContext->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
	}

	void GraphicsDeviceD3D11::CaptureState()
	{
		// Get the current state
		TODO("Implement this method");
	}

	void GraphicsDeviceD3D11::RestoreState()
	{
		// Reapply the previous state
		TODO("Implement this method");
	}

	void GraphicsDeviceD3D11::SetTransformMatrix(TransformType type, Matrix4 const & matrix)
	{
		// Change the transform values
		GraphicsDevice::SetTransformMatrix(type, matrix);
		m_matrixDirty = true;
	}

	TexturePtr GraphicsDeviceD3D11::CreateTexture(uint16 width, uint16 height)
	{
		return std::make_shared<TextureD3D11>(*this, width, height);
	}

	void GraphicsDeviceD3D11::BindTexture(TexturePtr texture, ShaderType shader, uint32 slot)
	{
		// TODO: Mark the BindTexture method obsolete? Technically it's no longer being needed
		texture->Bind(shader, slot);
	}

	void GraphicsDeviceD3D11::SetViewport(int32 x, int32 y, int32 w, int32 h, float minZ, float maxZ)
	{
		// Call super class method
		GraphicsDevice::SetViewport(x, y, w, h, minZ, maxZ);

		// Setup a viewport struct and apply
		D3D11_VIEWPORT vp;
		vp.TopLeftX = x;
		vp.TopLeftY = y;
		vp.Width = w;
		vp.Height = h;
		vp.MinDepth = minZ;
		vp.MaxDepth = maxZ;

		// Apply to current context
		m_immContext->RSSetViewports(1, &vp);
	}

	void GraphicsDeviceD3D11::SetClipRect(int32 x, int32 y, int32 w, int32 h)
	{
		D3D11_RECT clipRect;
		clipRect.left = x;
		clipRect.top = y;
		clipRect.right = x + w;
		clipRect.bottom = y + h;
		m_immContext->RSSetScissorRects(1, &clipRect);
		m_immContext->RSSetState(m_scissorRasterizerState.Get());
	}

	void GraphicsDeviceD3D11::ResetClipRect()
	{
		m_immContext->RSSetState(m_defaultRasterizerState.Get());
	}

	RenderWindowPtr GraphicsDeviceD3D11::CreateRenderWindow(std::string name, uint16 width, uint16 height)
	{
		return std::make_shared<RenderWindowD3D11>(*this, std::move(name), width, height);
	}

	RenderTexturePtr GraphicsDeviceD3D11::CreateRenderTexture(std::string name, uint16 width, uint16 height)
	{
		return std::make_shared<RenderTextureD3D11>(*this, std::move(name), width, height);
	}
}
