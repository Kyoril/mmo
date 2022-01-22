// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "graphics_device_d3d11.h"
#include "vertex_buffer_d3d11.h"
#include "index_buffer_d3d11.h"
#include "vertex_shader_d3d11.h"
#include "pixel_shader_d3d11.h"
#include "texture_d3d11.h"
#include "render_texture_d3d11.h"
#include "render_window_d3d11.h"
#include "rasterizer_state_hash.h"
#include "sampler_state_hash.h"

#include "base/macros.h"
#include "graphics/depth_stencil_hash.h"
#include "math/radian.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")


namespace mmo
{
	namespace
	{
		static D3D11_FILL_MODE D3D11FillMode(FillMode mode)
		{
			switch (mode)
			{
			case FillMode::Wireframe:
				return D3D11_FILL_WIREFRAME;
			}

			return D3D11_FILL_SOLID;
		}

		static D3D11_CULL_MODE D3D11CullMode(FaceCullMode mode)
		{
			switch (mode)
			{
			case FaceCullMode::Back:
				return D3D11_CULL_BACK;
			case FaceCullMode::Front:
				return D3D11_CULL_FRONT;
			}

			return D3D11_CULL_NONE;
		}

		static D3D11_TEXTURE_ADDRESS_MODE D3D11TextureAddressMode(TextureAddressMode mode)
		{
			switch (mode)
			{
			case TextureAddressMode::Clamp:
				return D3D11_TEXTURE_ADDRESS_CLAMP;
			case TextureAddressMode::Wrap:
				return D3D11_TEXTURE_ADDRESS_WRAP;
			case TextureAddressMode::Border:
				return D3D11_TEXTURE_ADDRESS_BORDER;
			case TextureAddressMode::Mirror:
				return D3D11_TEXTURE_ADDRESS_MIRROR;
			}

			return D3D11_TEXTURE_ADDRESS_CLAMP;
		}

		static D3D11_FILTER D3D11TextureFilter(TextureFilter mode)
		{
			switch (mode)
			{
			case TextureFilter::None:
				return D3D11_FILTER_MIN_MAG_MIP_POINT;
			case TextureFilter::Bilinear:
				return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			case TextureFilter::Trilinear:
				return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			case TextureFilter::Anisotropic:
				return D3D11_FILTER_ANISOTROPIC;
			}

			return D3D11_FILTER_ANISOTROPIC;
		}
	}

	
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

#ifdef _DEBUG
		m_device->QueryInterface(__uuidof(ID3D11Debug), &m_d3dDebug);
#endif
		
		// Create input layouts
		CreateInputLayouts();

		// Create blend states
		CreateBlendStates();

		// Setup constant buffers
		CreateConstantBuffers();

		// Create rasterizer state
		InitRasterizerState();

		// Create sampler state
		InitSamplerState();

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

	void GraphicsDeviceD3D11::InitRasterizerState()
	{
		ZeroMemory(&m_rasterizerDesc, sizeof(m_rasterizerDesc));
		m_rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		m_rasterizerDesc.CullMode = D3D11_CULL_NONE;
		m_rasterizerDescChanged = true;
	}

	void GraphicsDeviceD3D11::InitSamplerState()
	{
		ZeroMemory(&m_samplerDesc, sizeof(m_samplerDesc));
		m_samplerDesc.Filter = D3D11TextureFilter(m_texFilter);
		m_samplerDesc.MaxAnisotropy = D3D11_MAX_MAXANISOTROPY;
		m_samplerDesc.AddressU = D3D11TextureAddressMode(m_texAddressMode[0]);
		m_samplerDesc.AddressV = D3D11TextureAddressMode(m_texAddressMode[1]);
		m_samplerDesc.AddressW = D3D11TextureAddressMode(m_texAddressMode[2]);
		m_samplerDescChanged = true;
	}

	ID3D11SamplerState* GraphicsDeviceD3D11::CreateSamplerState()
	{
		// Create hash generator
		const SamplerStateHash hashGen;
		m_samplerHash = hashGen(m_samplerDesc);

		ComPtr<ID3D11SamplerState> state;
		VERIFY(SUCCEEDED(m_device->CreateSamplerState(&m_samplerDesc, &state)));
		m_samplerStates[m_samplerHash] = state;

		return state.Get();
	}

	void GraphicsDeviceD3D11::CreateDepthStates()
	{
		m_depthStencilDesc.DepthEnable = FALSE;
		m_depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		m_depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		m_depthStencilDesc.StencilEnable = FALSE;
		m_depthStencilDesc.StencilReadMask = 0xFF;
		m_depthStencilDesc.StencilWriteMask = 0xFF;

		// Stencil operations if pixel is front-facing
		m_depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		m_depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
		m_depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		m_depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		// Stencil operations if pixel is back-facing
		m_depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		m_depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
		m_depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		m_depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		
		m_depthStencilChanged = true;
	}

	void GraphicsDeviceD3D11::CreateRasterizerState(bool set)
	{
		// Create hash generator
		const RasterizerStateHash hashGen;

		// Generate hash from rasterizer desc
		m_rasterizerHash = hashGen(m_rasterizerDesc);
		
		ComPtr<ID3D11RasterizerState> state;
		VERIFY(SUCCEEDED(m_device->CreateRasterizerState(&m_rasterizerDesc, state.GetAddressOf())));
		m_rasterizerStates[m_rasterizerHash] = state;

		// Activate new state if requested
		if (set)
		{
			m_immContext->RSSetState(state.Get());
		}
	}

	void GraphicsDeviceD3D11::UpdateCurrentRasterizerState()
	{
		// Need rasterizer state update?
		if (m_rasterizerDescChanged)
		{
			// Calculate the current hash
			const RasterizerStateHash hashGen;
			m_rasterizerHash = hashGen(m_rasterizerDesc);

			m_rasterizerDescChanged = false;
		}
		
		// Check if rasterizer state for this hash has already been created
		auto it = m_rasterizerStates.find(m_rasterizerHash);
		if (it == m_rasterizerStates.end())
		{
			// Not yet created, so create and activate it
			CreateRasterizerState(true);
		}
		else
		{
			// State exists, so activate it
			m_immContext->RSSetState(it->second.Get());
		}
	}

	void GraphicsDeviceD3D11::UpdateDepthStencilState()
	{
		if (m_depthStencilChanged)
		{
			const auto hash = DepthStencilHash()(m_depthStencilDesc);
			if (hash != m_depthStencilHash)
			{
				auto it = m_depthStencilStates.find(hash);
				if (it == m_depthStencilStates.end())
				{
					ComPtr<ID3D11DepthStencilState> depthStencilState;
					VERIFY( SUCCEEDED(m_device->CreateDepthStencilState(&m_depthStencilDesc, depthStencilState.GetAddressOf())));

					it = m_depthStencilStates.emplace(hash, depthStencilState).first;
					ASSERT(it != m_depthStencilStates.end());
				}
				
				m_immContext->OMSetDepthStencilState(it->second.Get(), 0);
				m_depthStencilHash = hash;
			}
			else
			{
				const auto it = m_depthStencilStates.find(m_depthStencilHash);
				ASSERT(it != m_depthStencilStates.end());
				m_immContext->OMSetDepthStencilState(it->second.Get(), 0);
			}

			m_depthStencilChanged = false;
		}
	}

	ID3D11SamplerState* GraphicsDeviceD3D11::GetCurrentSamplerState()
	{
		ID3D11SamplerState* result = nullptr;

		if (m_samplerDescChanged)
		{
			const SamplerStateHash hashGen;
			m_samplerHash = hashGen(m_samplerDesc);
			m_samplerDescChanged = false;
		}

		// Check if sampler state for this hash has already been created
		const auto it = m_samplerStates.find(m_samplerHash);
		if (it == m_samplerStates.end())
		{
			result = CreateSamplerState();
		}
		else
		{
			result = it->second.Get();
		}

		return result;
	}

	GraphicsDeviceD3D11::GraphicsDeviceD3D11()
		: m_rasterizerDesc()
		, m_samplerDesc()
	{
	}

	GraphicsDeviceD3D11::~GraphicsDeviceD3D11()
	{
#ifdef _DEBUG
		if (m_d3dDebug)
		{
			m_d3dDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
		}
#endif
	}

	Matrix4 GraphicsDeviceD3D11::MakeProjectionMatrix(const Radian& fovY, float aspect, float nearPlane, float farPlane)
	{
		Matrix4 dest = Matrix4::Zero;

		const float theta = fovY.GetValueRadians() * 0.5f;
		float h = 1 / std::tan(theta);
		float w = h / aspect;
		float q, qn;
		
		q = farPlane / (farPlane - nearPlane);
		qn = -q * nearPlane;

		dest[0][0] = w;
		dest[1][1] = h;
		dest[2][2] = -q;
		dest[3][2] = -1.0f;
		dest[2][3] = qn;

		return dest;
	}

	Matrix4 GraphicsDeviceD3D11::MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearPlane, float farPlane)
	{
		const float inv_w = 1 / (right - left);
		const float inv_h = 1 / (top - bottom);
		const float inv_d = 1 / (farPlane - nearPlane);

		const float A = 2 * inv_w;
		const float B = 2 * inv_h;
		const float C = - (right + left) * inv_w;
		const float D = - (top + bottom) * inv_h;

		const float q = - 2 * inv_d;
		const float qn = - (farPlane + nearPlane) * inv_d;
		
		Matrix4 result = Matrix4::Zero;
		result[0][0] = A;
		result[0][3] = C;
		result[1][1] = B;
		result[1][3] = D;
		result[2][2] = q;
		result[2][3] = qn;
		result[3][3] = 1;
		return result;
	}

	void GraphicsDeviceD3D11::Reset()
	{
		// Clear the state
		m_immContext->ClearState();

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

		// Rasterizer state
		UpdateCurrentRasterizerState();
		UpdateDepthStencilState();
	
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

		// Apply vertical sync value
		m_vsync = desc.vsync;

		// Initialize Direct3D
		CreateD3D11();

		// Create an automatic render window if requested
		if (desc.customWindowHandle == nullptr)
		{
			m_autoCreatedWindow = CreateRenderWindow("__auto_window__", desc.width, desc.height, !desc.windowed);
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

	void GraphicsDeviceD3D11::UpdateSamplerState()
	{
		if (m_samplerDescChanged)
		{
			const SamplerStateHash hashGen;
			m_samplerHash = hashGen(m_samplerDesc);

			// Set sampler state
			ID3D11SamplerState* const samplerStates = GetCurrentSamplerState();
			m_immContext->PSSetSamplers(0, 1, &samplerStates);
			m_samplerDescChanged = false;
		}
	}

	void GraphicsDeviceD3D11::Draw(uint32 vertexCount, uint32 start)
	{
		UpdateCurrentRasterizerState();
		UpdateDepthStencilState();

		// Update the constant buffer
		m_matrixDirty = false;
		m_immContext->UpdateSubresource(m_matrixBuffer.Get(), 0, 0, &m_transform, 0, 0);
		
		// Execute draw command
		m_immContext->Draw(vertexCount, start);
	}

	void GraphicsDeviceD3D11::DrawIndexed()
	{
		UpdateCurrentRasterizerState();
		UpdateDepthStencilState();
		
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
		GraphicsDevice::SetTopologyType(InType);

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
		}
	}

	void GraphicsDeviceD3D11::SetBlendMode(BlendMode InBlendMode)
	{
		GraphicsDevice::SetBlendMode(InBlendMode);

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
		GraphicsDevice::CaptureState();
	}

	void GraphicsDeviceD3D11::RestoreState()
	{
		GraphicsDevice::RestoreState();
		m_matrixDirty = true;
		m_samplerDescChanged = true;
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
		
		ID3D11SamplerState* const samplerStates = GetCurrentSamplerState();
		m_immContext->PSSetSamplers(0, 1, &samplerStates);
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

		m_rasterizerDesc.ScissorEnable = TRUE;
		m_rasterizerDescChanged = true;
	}

	void GraphicsDeviceD3D11::ResetClipRect()
	{
		m_rasterizerDesc.ScissorEnable = FALSE;
		m_rasterizerDescChanged = true;
	}

	RenderWindowPtr GraphicsDeviceD3D11::CreateRenderWindow(std::string name, uint16 width, uint16 height, bool fullScreen)
	{
		return std::make_shared<RenderWindowD3D11>(*this, std::move(name), width, height, fullScreen);
	}

	RenderTexturePtr GraphicsDeviceD3D11::CreateRenderTexture(std::string name, uint16 width, uint16 height)
	{
		return std::make_shared<RenderTextureD3D11>(*this, std::move(name), width, height);
	}

	void GraphicsDeviceD3D11::SetFillMode(FillMode mode)
	{
		GraphicsDevice::SetFillMode(mode);

		m_rasterizerDesc.FillMode = D3D11FillMode(mode);
		m_rasterizerDescChanged = true;
	}

	void GraphicsDeviceD3D11::SetFaceCullMode(FaceCullMode mode)
	{
		GraphicsDevice::SetFaceCullMode(mode);

		m_rasterizerDesc.CullMode = D3D11CullMode(mode);
		m_rasterizerDescChanged = true;
	}

	void GraphicsDeviceD3D11::SetTextureAddressMode(const TextureAddressMode modeU, const TextureAddressMode modeV, const TextureAddressMode modeW)
	{
		GraphicsDevice::SetTextureAddressMode(modeU, modeV, modeW);
		
		m_samplerDesc.AddressU = D3D11TextureAddressMode(modeU);
		m_samplerDesc.AddressV = D3D11TextureAddressMode(modeV);
		m_samplerDesc.AddressW = D3D11TextureAddressMode(modeW);
		m_samplerDescChanged = true;
	}
	
	void GraphicsDeviceD3D11::SetTextureFilter(const TextureFilter filter)
	{
		GraphicsDevice::SetTextureFilter(filter);

		m_samplerDesc.Filter = D3D11TextureFilter(filter);
		m_samplerDescChanged = true;
	}

	void GraphicsDeviceD3D11::SetDepthEnabled(const bool enable)
	{
		GraphicsDevice::SetDepthEnabled(enable);

		m_depthStencilDesc.DepthEnable = enable ? TRUE : FALSE;
		m_depthStencilChanged = true;
	}

	void GraphicsDeviceD3D11::SetDepthWriteEnabled(const bool enable)
	{
		GraphicsDevice::SetDepthWriteEnabled(enable);

		m_depthStencilDesc.DepthWriteMask = enable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
		m_depthStencilChanged = true;
	}

	namespace
	{
		inline D3D11_COMPARISON_FUNC MapComparison(const DepthTestMethod comparison)
		{
			switch(comparison)
			{
			case DepthTestMethod::Never: return D3D11_COMPARISON_NEVER;
			case DepthTestMethod::Less: return D3D11_COMPARISON_LESS;
			case DepthTestMethod::Equal: return D3D11_COMPARISON_EQUAL;
			case DepthTestMethod::LessEqual: return D3D11_COMPARISON_LESS_EQUAL;;
			case DepthTestMethod::Greater: return D3D11_COMPARISON_GREATER;
			case DepthTestMethod::NotEqual: return D3D11_COMPARISON_NOT_EQUAL;
			case DepthTestMethod::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
			}

			return D3D11_COMPARISON_ALWAYS;
		}
	}

	void GraphicsDeviceD3D11::SetDepthTestComparison(const DepthTestMethod comparison)
	{
		GraphicsDevice::SetDepthTestComparison(comparison);
		
		m_depthStencilDesc.DepthFunc = MapComparison(comparison);
		m_depthStencilChanged = true;
	}
}
