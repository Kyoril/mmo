// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graphics_device_d3d11.h"

#include "constant_buffer_d3d11.h"
#include "index_buffer_d3d11.h"
#include "material_compiler_d3d11.h"
#include "pixel_shader_d3d11.h"
#include "rasterizer_state_hash.h"
#include "render_texture_d3d11.h"
#include "render_window_d3d11.h"
#include "sampler_state_hash.h"
#include "shader_compiler_d3d11.h"
#include "texture_d3d11.h"
#include "vertex_buffer_d3d11.h"
#include "vertex_shader_d3d11.h"
#include "vertex_declaration_d3d11.h"

#include "base/macros.h"
#include "graphics/depth_stencil_hash.h"
#include "log/default_log_levels.h"
#include "luabind/operator.hpp"
#include "math/radian.h"
#include "scene_graph/render_operation.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifdef _DEBUG
#ifndef MMO_GPU_DEBUG
#	define MMO_GPU_DEBUG 1
#endif
#else
#	define MMO_GPU_DEBUG 0
#endif

namespace mmo
{
	namespace
	{
		constexpr D3D11_FILL_MODE D3D11FillMode(const FillMode mode)
		{
			return (mode == FillMode::Wireframe) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
		}

		constexpr D3D11_CULL_MODE D3D11CullMode(const FaceCullMode mode)
		{
			switch (mode)
			{
			case FaceCullMode::Back: return D3D11_CULL_BACK;
			case FaceCullMode::Front: return D3D11_CULL_FRONT;
			default: return D3D11_CULL_NONE;
			}
		}

		// Use a lookup table for TextureAddressMode
		constexpr D3D11_TEXTURE_ADDRESS_MODE TextureAddressModeTable[] = {
			D3D11_TEXTURE_ADDRESS_CLAMP,  // TextureAddressMode::Clamp
			D3D11_TEXTURE_ADDRESS_WRAP,   // TextureAddressMode::Wrap
			D3D11_TEXTURE_ADDRESS_BORDER, // TextureAddressMode::Border
			D3D11_TEXTURE_ADDRESS_MIRROR  // TextureAddressMode::Mirror
		};

		constexpr D3D11_TEXTURE_ADDRESS_MODE D3D11TextureAddressMode(const TextureAddressMode mode)
		{
			return (static_cast<int>(mode) >= 0 && static_cast<int>(mode) < std::size(TextureAddressModeTable)) ? TextureAddressModeTable[static_cast<int>(mode)] : D3D11_TEXTURE_ADDRESS_CLAMP;
		}

		// Use a lookup table for TextureFilter
		constexpr D3D11_FILTER TextureFilterTable[] = {
			D3D11_FILTER_MIN_MAG_MIP_POINT,       // TextureFilter::None
			D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, // TextureFilter::Bilinear
			D3D11_FILTER_MIN_MAG_MIP_LINEAR,      // TextureFilter::Trilinear
			D3D11_FILTER_ANISOTROPIC              // TextureFilter::Anisotropic
		};

		constexpr D3D11_FILTER D3D11TextureFilter(const TextureFilter mode)
		{
			return (static_cast<int>(mode) >= 0 && static_cast<int>(mode) < std::size(TextureFilterTable)) ? TextureFilterTable[static_cast<int>(mode)] : D3D11_FILTER_ANISOTROPIC;
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
		constexpr D3D_FEATURE_LEVEL supportedFeatureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1 };

		// Setup device flags
		UINT deviceCreationFlags = 0;

#if MMO_GPU_DEBUG
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

#if MMO_GPU_DEBUG
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
		#include "shaders/VS_PosColorNormalBinormalTangentTex.h"
		#include "shaders/VS_PosColorTex.h"

		// Setup vertex shaders
		VertexShaders[VertexFormat::Pos] = CreateShader(ShaderType::VertexShader, g_VS_Pos, ARRAYSIZE(g_VS_Pos));
		VertexShaders[VertexFormat::PosColor] = CreateShader(ShaderType::VertexShader, g_VS_PosColor, ARRAYSIZE(g_VS_PosColor));
		VertexShaders[VertexFormat::PosColorNormal] = CreateShader(ShaderType::VertexShader, g_VS_PosColorNormal, ARRAYSIZE(g_VS_PosColorNormal));
		VertexShaders[VertexFormat::PosColorNormalTex1] = CreateShader(ShaderType::VertexShader, g_VS_PosColorNormalTex, ARRAYSIZE(g_VS_PosColorNormalTex));
		VertexShaders[VertexFormat::PosColorNormalBinormalTangentTex1] = CreateShader(ShaderType::VertexShader, g_VS_PosColorNormalBinormalTangentTex, ARRAYSIZE(g_VS_PosColorNormalBinormalTangentTex));
		VertexShaders[VertexFormat::PosColorTex1] = CreateShader(ShaderType::VertexShader, g_VS_PosColorTex, ARRAYSIZE(g_VS_PosColorTex));

		#include "shaders/PS_Pos.h"
		#include "shaders/PS_PosColor.h"
		#include "shaders/PS_PosColorNormal.h"
		#include "shaders/PS_PosColorNormalTex.h"
		#include "shaders/PS_PosColorNormalBinormalTangentTex.h"
		#include "shaders/PS_PosColorTex.h"

		// Setup pixel shaders
		PixelShaders[VertexFormat::Pos] = CreateShader(ShaderType::PixelShader, g_PS_Pos, ARRAYSIZE(g_PS_Pos));
		PixelShaders[VertexFormat::PosColor] = CreateShader(ShaderType::PixelShader, g_PS_PosColor, ARRAYSIZE(g_PS_PosColor));
		PixelShaders[VertexFormat::PosColorNormal] = CreateShader(ShaderType::PixelShader, g_PS_PosColorNormal, ARRAYSIZE(g_PS_PosColorNormal));
		PixelShaders[VertexFormat::PosColorNormalTex1] = CreateShader(ShaderType::PixelShader, g_PS_PosColorNormalTex, ARRAYSIZE(g_PS_PosColorNormalTex));
		PixelShaders[VertexFormat::PosColorNormalBinormalTangentTex1] = CreateShader(ShaderType::PixelShader, g_PS_PosColorNormalBinormalTangentTex, ARRAYSIZE(g_PS_PosColorNormalBinormalTangentTex));
		PixelShaders[VertexFormat::PosColorTex1] = CreateShader(ShaderType::PixelShader, g_PS_PosColorTex, ARRAYSIZE(g_PS_PosColorTex));

		ComPtr<ID3D11InputLayout> InputLayout;

		// EGxVertexFormat::Pos
		const D3D11_INPUT_ELEMENT_DESC PosElements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		VERIFY(SUCCEEDED(m_device->CreateInputLayout(PosElements, ARRAYSIZE(PosElements), g_VS_Pos, ARRAYSIZE(g_VS_Pos), &InputLayout)));
		InputLayouts[VertexFormat::Pos] = InputLayout;

		// EGxVertexFormat::PosColor
		const D3D11_INPUT_ELEMENT_DESC PosColElements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		VERIFY(SUCCEEDED(m_device->CreateInputLayout(PosColElements, ARRAYSIZE(PosColElements), g_VS_PosColor, ARRAYSIZE(g_VS_PosColor), &InputLayout)));
		InputLayouts[VertexFormat::PosColor] = InputLayout;

		// EGxVertexFormat::PosColorNormal
		const D3D11_INPUT_ELEMENT_DESC PosColNormElements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		VERIFY(SUCCEEDED(m_device->CreateInputLayout(PosColNormElements, ARRAYSIZE(PosColNormElements), g_VS_PosColorNormal, ARRAYSIZE(g_VS_PosColorNormal), &InputLayout)));
		InputLayouts[VertexFormat::PosColorNormal] = InputLayout;

		// EGxVertexFormat::PosColorNormalTex1
		const D3D11_INPUT_ELEMENT_DESC PosColNormTexElements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		VERIFY(SUCCEEDED(m_device->CreateInputLayout(PosColNormTexElements, ARRAYSIZE(PosColNormTexElements), g_VS_PosColorNormalTex, ARRAYSIZE(g_VS_PosColorNormalTex), &InputLayout)));
		InputLayouts[VertexFormat::PosColorNormalTex1] = InputLayout;
		
		// EGxVertexFormat::PosColorNormalBinormalTangentTex1
		const D3D11_INPUT_ELEMENT_DESC PosColNormBinormalTangentTexElements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		VERIFY(SUCCEEDED(m_device->CreateInputLayout(PosColNormBinormalTangentTexElements, ARRAYSIZE(PosColNormBinormalTangentTexElements), g_VS_PosColorNormalBinormalTangentTex, ARRAYSIZE(g_VS_PosColorNormalBinormalTangentTex), &InputLayout)));
		InputLayouts[VertexFormat::PosColorNormalBinormalTangentTex1] = InputLayout;

		// EGxVertexFormat::PosColorTex1
		const D3D11_INPUT_ELEMENT_DESC PosColTexElements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
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
		m_inverseView = Matrix4::Identity.Inverse();
		m_inverseProj = Matrix4::Identity.Inverse();

		D3D11_BUFFER_DESC cbd;
		ZeroMemory(&cbd, sizeof(cbd));
		cbd.Usage = D3D11_USAGE_DYNAMIC;
		cbd.ByteWidth = sizeof(Matrix4) * 5;
		cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

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
		m_samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		m_samplerDescChanged = true;
	}

	ID3D11SamplerState* GraphicsDeviceD3D11::CreateSamplerState()
	{
		// Create hash generator
		constexpr SamplerStateHash hashGen;
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

	void GraphicsDeviceD3D11::CreateRasterizerState(const bool set)
	{
		// Create hash generator
		constexpr RasterizerStateHash hashGen;

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
			constexpr RasterizerStateHash hashGen;
			m_rasterizerHash = hashGen(m_rasterizerDesc);

			m_rasterizerDescChanged = false;
		}
		
		// Check if rasterizer state for this hash has already been created
		const auto it = m_rasterizerStates.find(m_rasterizerHash);
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
			constexpr SamplerStateHash hashGen;
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

	void GraphicsDeviceD3D11::UpdateMatrixBuffer()
	{
		D3D11_MAPPED_SUBRESOURCE mappedResource;
		const HRESULT hr = m_immContext->Map(m_matrixBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
		if (SUCCEEDED(hr))
		{
			memcpy(mappedResource.pData, m_transform, sizeof(m_transform));
			memcpy(static_cast<uint8*>(mappedResource.pData) + sizeof(m_transform), &m_inverseView, sizeof(m_inverseView));
			memcpy(static_cast<uint8*>(mappedResource.pData) + sizeof(m_transform) + sizeof(m_inverseView), &m_inverseProj, sizeof(m_inverseProj));
			m_immContext->Unmap(m_matrixBuffer.Get(), 0);

#if MMO_GPU_DEBUG
			// Double check here
			ASSERT(Matrix4::Identity.IsNearlyEqual(m_inverseView * m_transform[View]));
			//ASSERT(Matrix4::Identity.IsNearlyEqual(m_inverseProj * m_transform[Projection]));
#endif
		}
		else
		{
			ASSERT(false);
		}
	}

	ID3D11InputLayout* GraphicsDeviceD3D11::GetOrCreateInputLayout(
		const VertexDeclaration* vertexDecl, 
		VertexShaderD3D11* shader,
		const std::vector<D3D11_INPUT_ELEMENT_DESC>& elements)
	{
		// Create the cache key
		InputLayoutCacheKey key{vertexDecl->GetHash(), shader};
		
		// Check if we already have this input layout
		auto it = m_inputLayoutCache.find(key);
		if (it != m_inputLayoutCache.end())
		{
			return it->second.Get();
		}
		
		// Create a new input layout
		ComPtr<ID3D11InputLayout> inputLayout;
		const auto& microcode = shader->GetByteCode();
		
		HRESULT hr = m_device->CreateInputLayout(
			elements.data(), 
			elements.size(), 
			microcode.data(), 
			microcode.size(), 
			&inputLayout);
			
		if (SUCCEEDED(hr))
		{
			// Add to cache and return
			m_inputLayoutCache[key] = inputLayout;
			return inputLayout.Get();
		}
		
		// Return null on failure
		return nullptr;
	}
	
	D3D11_MAP MapLockOptionsToD3D11(LockOptions options)
	{
		switch(options)
		{
		case LockOptions::Discard:
			return D3D11_MAP_WRITE_DISCARD;
		case LockOptions::NoOverwrite:
			return D3D11_MAP_WRITE_NO_OVERWRITE;
		case LockOptions::WriteOnly:
			return D3D11_MAP_WRITE;
		case LockOptions::ReadOnly:
			return D3D11_MAP_READ;
		case LockOptions::Normal:
			return D3D11_MAP_WRITE_DISCARD;
		}

		UNREACHABLE();
		return D3D11_MAP_WRITE_DISCARD;
	}

	GraphicsDeviceD3D11::GraphicsDeviceD3D11()
		: m_rasterizerDesc()
		  , m_samplerDesc(), m_depthStencilDesc()
	{
	}

	GraphicsDeviceD3D11::~GraphicsDeviceD3D11()
	{
		InputLayouts.clear();
		VertexShaders.clear();
		PixelShaders.clear();
		m_renderTarget.reset();

		m_matrixBuffer.Reset();
		m_rasterizerStates.clear();
		m_samplerStates.clear();
		m_depthStencilStates.clear();

		m_alphaBlendState.Reset();
		m_opaqueBlendState.Reset();
		m_immContext.Reset();
		m_device.Reset();

#if MMO_GPU_DEBUG
		if (m_d3dDebug)
		{
			m_d3dDebug->ReportLiveDeviceObjects(D3D11_RLDO_IGNORE_INTERNAL);
		}
#endif
	}

	Matrix4 GraphicsDeviceD3D11::MakeProjectionMatrix(const Radian& fovY, const float aspect, const float nearPlane, const float farPlane)
	{
		Matrix4 dest = Matrix4::Zero;

		const float theta = fovY.GetValueRadians() * 0.5f;
		const float h = 1 / std::tan(theta);
		const float w = h / aspect;

		const float q = farPlane / (farPlane - nearPlane);
		const float qn = -q * nearPlane;

		dest[0][0] = w;
		dest[1][1] = h;
		dest[2][2] = -q;
		dest[3][2] = -1.0f;
		dest[2][3] = qn;

		return dest;
	}

	Matrix4 GraphicsDeviceD3D11::MakeOrthographicMatrix(const float left, const float top, const float right, const float bottom, const float nearPlane, const float farPlane)
	{
		const float invW = 1.0f / (right - left);
		const float invH = 1.0f / (top - bottom);
		const float invD = 1.0f / (farPlane - nearPlane);

		Matrix4 result = Matrix4::Zero;
		result[0][0] = 2.0f * invW;
		result[0][3] = -(right + left) * invW;
		result[1][1] = 2.0f * invH;
		result[1][3] = -(top + bottom) * invH;
		result[2][2] = invD;
		result[2][3] = -nearPlane * invD;
		result[3][3] = 1.0f;

		return result;
	}

	void GraphicsDeviceD3D11::Reset()
	{
		// Clear the state
		m_immContext->ClearState();

		m_vertexFormat = VertexFormat::Last;
		m_blendMode = BlendMode::Undefined;
		m_topologyType = TopologyType::Undefined;

		// Reset rasterizer
		m_rasterizerDesc.DepthBias = 0;
		m_rasterizerDesc.DepthBiasClamp = 0.0f;
		m_rasterizerDesc.SlopeScaledDepthBias = 0.0f;
		m_rasterizerDescChanged = true;

		m_lastInputLayout = nullptr;

		m_lastFrameBatchCount = m_batchCount;
		m_batchCount = 0;

		// Update the constant buffer
		if (m_matrixDirty)
		{
			// Reset transforms
			m_transform[0] = Matrix4::Identity;
			m_transform[1] = Matrix4::Identity;
			m_transform[2] = Matrix4::Identity;
			m_inverseView = m_transform[1].InverseAffine();
			m_inverseProj = m_transform[2].Inverse();
			UpdateMatrixBuffer();
			m_matrixDirty = false;
		}

		// Reset bound texture slots
		for (size_t i = 0; i < std::size(m_textureSlots); ++i)
		{
			m_textureSlots[i] = nullptr;
		}

		m_immContext->VSSetShader(nullptr, nullptr, 0);
		m_immContext->PSSetShader(nullptr, nullptr, 0);

		// Set the constant buffers
		ID3D11Buffer* Buffers[] = { m_matrixBuffer.Get() };
		m_immContext->VSSetConstantBuffers(0, 1, Buffers);
		m_immContext->PSSetConstantBuffers(0, 1, Buffers);

		// Default blend state
		m_immContext->OMSetBlendState(m_opaqueBlendState.Get(), nullptr, 0xffffffff);

		// Rasterizer state
		UpdateCurrentRasterizerState();
		UpdateDepthStencilState();

		// Warning: By default we have no active render target nor any viewport set. This needs to be done afterwards
	}

	void GraphicsDeviceD3D11::SetClearColor(const uint32 clearColor)
	{
		// Set clear color value in base device
		GraphicsDevice::SetClearColor(clearColor);

		// Calculate d3d11 float clear color values
		const BYTE r = GetRValue(clearColor);
		const BYTE g = GetGValue(clearColor);
		const BYTE b = GetBValue(clearColor);
		m_clearColorFloat[0] = static_cast<float>(r) / 255.0f;
		m_clearColorFloat[1] = static_cast<float>(g) / 255.0f;
		m_clearColorFloat[2] = static_cast<float>(b) / 255.0f;
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
			m_autoCreatedWindow = std::make_shared<RenderWindowD3D11>(*this, "__auto_window__", static_cast<HWND>(desc.customWindowHandle));
		}
	}

	void GraphicsDeviceD3D11::Clear(const ClearFlags flags)
	{
		// Reset the current state
		Reset();

		// Set the current render target
		m_autoCreatedWindow->Activate();
		m_autoCreatedWindow->Clear(flags);
	}

	VertexBufferPtr GraphicsDeviceD3D11::CreateVertexBuffer(size_t vertexCount, size_t vertexSize, BufferUsage usage, const void * initialData)
	{
		return std::make_shared<VertexBufferD3D11>(*this, vertexCount, vertexSize, usage, initialData);
	}

	IndexBufferPtr GraphicsDeviceD3D11::CreateIndexBuffer(size_t indexCount, IndexBufferSize indexSize, BufferUsage usage, const void * initialData)
	{
		return std::make_shared<IndexBufferD3D11>(*this, indexCount, indexSize, usage, initialData);
	}

	ConstantBufferPtr GraphicsDeviceD3D11::CreateConstantBuffer(size_t size, const void* initialData)
	{
		return std::make_shared<ConstantBufferD3D11>(*this, *this, size, initialData);
	}

	ShaderPtr GraphicsDeviceD3D11::CreateShader(const ShaderType type, const void * shaderCode, size_t shaderCodeSize)
	{
		switch (type)
		{
		case ShaderType::VertexShader:
			return std::make_unique<VertexShaderD3D11>(*this, shaderCode, shaderCodeSize);
		case ShaderType::PixelShader:
			return std::make_unique<PixelShaderD3D11>(*this, shaderCode, shaderCodeSize);
		default:
			ASSERT(! "This shader type can't yet be created - implement it for D3D11!");
		}

		return nullptr;
	}

	void GraphicsDeviceD3D11::SetDepthBias(float bias)
	{
		m_rasterizerDesc.DepthBias = static_cast<int32>(bias);
		m_rasterizerDescChanged = true;
	}

	void GraphicsDeviceD3D11::SetSlopeScaledDepthBias(float bias)
	{
		m_rasterizerDesc.SlopeScaledDepthBias = bias;
		m_rasterizerDescChanged = true;
	}

	void GraphicsDeviceD3D11::SetDepthBiasClamp(float bias)
	{
		m_rasterizerDesc.DepthBiasClamp = bias;
		m_rasterizerDescChanged = true;
	}

	void GraphicsDeviceD3D11::UpdateSamplerState()
	{
		if (m_samplerDescChanged)
		{
			constexpr SamplerStateHash hashGen;
			m_samplerHash = hashGen(m_samplerDesc);

			// Set sampler state
			ID3D11SamplerState* const samplerStates = GetCurrentSamplerState();
			m_immContext->PSSetSamplers(0, 1, &samplerStates);
			m_samplerDescChanged = false;
		}
	}

	void GraphicsDeviceD3D11::Draw(const uint32 vertexCount, const uint32 start)
	{
		UpdateCurrentRasterizerState();
		UpdateDepthStencilState();
		UpdateSamplerState();

		if (m_matrixDirty)
		{
			// Update the constant buffer
			m_matrixDirty = false;
			UpdateMatrixBuffer();
		}
		
		// Execute draw command
		m_immContext->Draw(vertexCount, start);
		m_batchCount++;
	}

	void GraphicsDeviceD3D11::DrawIndexed(const uint32 startIndex, const uint32 endIndex)
	{
		UpdateCurrentRasterizerState();
		UpdateDepthStencilState();
		UpdateSamplerState();
		
		if (m_matrixDirty)
		{
			// Update the constant buffer
			m_matrixDirty = false;
			UpdateMatrixBuffer();
		}
		
		// Execute draw command
		m_immContext->DrawIndexed(endIndex == 0 ? m_indexCount - startIndex : endIndex - startIndex, startIndex, 0);
		m_batchCount++;
	}

	void GraphicsDeviceD3D11::DrawIndexedInstanced(const uint32 indexCount, const uint32 instanceCount, const uint32 startIndex, const int32 baseVertex, const uint32 startInstance)
	{
		UpdateCurrentRasterizerState();
		UpdateDepthStencilState();
		UpdateSamplerState();

		if (m_matrixDirty)
		{
			m_matrixDirty = false;
			UpdateMatrixBuffer();
		}

		// Execute instanced draw command
		m_immContext->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
		m_batchCount++;
	}

	namespace
	{
		/// Determines the D3D11_PRIMITIVE_TOPOLOGY value from an EGxTopologyType value.
		D3D11_PRIMITIVE_TOPOLOGY D3DTopologyType(const TopologyType type)
		{
			switch (type)
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

	void GraphicsDeviceD3D11::SetTopologyType(const TopologyType type)
	{
		if (m_topologyType == type)
		{
			return;
		}

		GraphicsDevice::SetTopologyType(type);

		const D3D11_PRIMITIVE_TOPOLOGY topology = D3DTopologyType(type);
		ASSERT(topology != D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);

		m_immContext->IASetPrimitiveTopology(topology);
	}

	void GraphicsDeviceD3D11::SetVertexFormat(const VertexFormat format)
	{
		m_vertexFormat = format;

		const auto it = InputLayouts.find(format);
		ASSERT(it != InputLayouts.end());

		if (m_lastInputLayout != it->second.Get())
		{
			m_lastInputLayout = it->second.Get();
			m_immContext->IASetInputLayout(it->second.Get());

			const auto vertexShaderIt = VertexShaders.find(format);
			if (vertexShaderIt != VertexShaders.end())
			{
				vertexShaderIt->second->Set();
			}

			const auto pixelShaderIt = PixelShaders.find(format);
			if (pixelShaderIt != PixelShaders.end())
			{
				pixelShaderIt->second->Set();
			}
		}
	}

	void GraphicsDeviceD3D11::SetBlendMode(const BlendMode blendMode)
	{
		if (m_blendMode == blendMode)
		{
			return;
		}

		GraphicsDevice::SetBlendMode(blendMode);

		ID3D11BlendState* blendState = nullptr;

		// Set blend state
		switch (blendMode)
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

		m_restoreInverseView = m_inverseView;
		m_restoreInverseProj = m_inverseProj;
	}

	void GraphicsDeviceD3D11::RestoreState()
	{
		GraphicsDevice::RestoreState();
		
		m_inverseView = m_restoreInverseView;
		m_inverseProj = m_restoreInverseProj;

		m_samplerDescChanged = true;
		m_lastInputLayout = nullptr;
		m_matrixDirty = true;

		// Invalidate texture slot cache
		for (size_t i = 0; i < std::size(m_textureSlots); ++i)
		{
			m_textureSlots[i] = nullptr;
		}
	}

	void GraphicsDeviceD3D11::SetTransformMatrix(const TransformType type, Matrix4 const & matrix)
	{
		if (GetTransformMatrix(type) == matrix)
		{
			return;
		}

		// Change the transform values
		GraphicsDevice::SetTransformMatrix(type, matrix);

		if (type == View)
		{
			m_inverseView = m_transform[View].InverseAffine();
#if MMO_GPU_DEBUG
			// First check on set (fail early)
			ASSERT(Matrix4::Identity.IsNearlyEqual(m_inverseView * m_transform[View]));			
#endif
		}
		else if (type == Projection)
		{
			m_inverseProj = m_transform[Projection].Inverse();
#if MMO_GPU_DEBUG
			// First check on set (fail early)
			//ASSERT(Matrix4::Identity.IsNearlyEqual(m_inverseProj * m_transform[Projection]));
#endif
		}

		m_matrixDirty = true;
	}

	TexturePtr GraphicsDeviceD3D11::CreateTexture(uint16 width, uint16 height, BufferUsage usage)
	{
		return std::make_shared<TextureD3D11>(*this, width, height, usage);
	}

	void GraphicsDeviceD3D11::BindTexture(const TexturePtr texture, const ShaderType shader, const uint32 slot)
	{
		ASSERT(slot < std::size(m_textureSlots));

		if (!texture)
		{
			m_textureSlots[slot] = nullptr;
			return;
		}

		// Texture already bound to slot? Then nothing to do here
		if (m_textureSlots[slot] == texture.get())
		{
			return;
		}

		// TODO: Mark the BindTexture method obsolete? Technically it's no longer being needed
		texture->Bind(shader, slot);
		
		ID3D11SamplerState* const samplerStates = GetCurrentSamplerState();
		m_immContext->PSSetSamplers(slot, 1, &samplerStates);
	}

	void GraphicsDeviceD3D11::SetViewport(const int32 x, const int32 y, const int32 w, const int32 h, const float minZ, const float maxZ)
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

	void GraphicsDeviceD3D11::SetClipRect(const int32 x, const int32 y, const int32 w, const int32 h)
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

	RenderTexturePtr GraphicsDeviceD3D11::CreateRenderTexture(std::string name, uint16 width, uint16 height, RenderTextureFlags flags, PixelFormat colorFormat, PixelFormat depthFormat)
	{
		return std::make_shared<RenderTextureD3D11>(*this, std::move(name), width, height, flags, colorFormat, depthFormat);
	}

	void GraphicsDeviceD3D11::SetRenderTargets(RenderTexturePtr* renderTargets, uint32 count)
	{
		ASSERT(count <= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
		//ASSERT(renderTargets != nullptr);

		// Collect render target views
		ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		for (uint32 i = 0; i < count; ++i)
		{
			ASSERT(renderTargets[i]);
			auto* rtd3d11 = static_cast<RenderTextureD3D11*>(renderTargets[i].get());
			rtvs[i] = rtd3d11->GetRenderTargetView();
		}

		// Set render targets
		m_immContext->OMSetRenderTargets(count, rtvs, nullptr);
	}

	void GraphicsDeviceD3D11::SetRenderTargetsWithDepthStencil(RenderTexturePtr* renderTargets, uint32 count, RenderTexturePtr depthStencilRT)
	{
		ASSERT(count <= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
		ASSERT(depthStencilRT);

		// Collect render target views
		ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		for (uint32 i = 0; i < count; ++i)
		{
			ASSERT(renderTargets[i]);
			auto* rtd3d11 = static_cast<RenderTextureD3D11*>(renderTargets[i].get());
			rtvs[i] = rtd3d11->GetRenderTargetView();
		}

		// Get the depth stencil view
		auto* depthStencilD3D11 = static_cast<RenderTextureD3D11*>(depthStencilRT.get());
		ID3D11DepthStencilView* dsv = depthStencilD3D11->GetDepthStencilView();
		ASSERT(dsv);

		// Set render targets with depth stencil
		m_immContext->OMSetRenderTargets(count, rtvs, dsv);
	}

	void GraphicsDeviceD3D11::SetFillMode(const FillMode mode)
	{
		if (m_fillMode == mode)
		{
			return;
		}

		GraphicsDevice::SetFillMode(mode);

		m_rasterizerDesc.FillMode = D3D11FillMode(mode);
		m_rasterizerDescChanged = true;
	}

	void GraphicsDeviceD3D11::SetFaceCullMode(const FaceCullMode mode)
	{
		if (m_cullMode == mode)
		{
			return;
		}

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
		D3D11_COMPARISON_FUNC MapComparison(const DepthTestMethod comparison)
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

	std::unique_ptr<MaterialCompiler> GraphicsDeviceD3D11::CreateMaterialCompiler()
	{
		return std::make_unique<MaterialCompilerD3D11>();
	}

	std::unique_ptr<ShaderCompiler> GraphicsDeviceD3D11::CreateShaderCompiler()
	{
		return std::make_unique<ShaderCompilerD3D11>();
	}

	VertexDeclaration* GraphicsDeviceD3D11::CreateVertexDeclaration()
	{
		return m_vertexDeclarations.emplace_back(std::make_unique<VertexDeclarationD3D11>(*this)).get();
	}

	VertexBufferBinding* GraphicsDeviceD3D11::CreateVertexBufferBinding()
	{
		return GraphicsDevice::CreateVertexBufferBinding();
	}

	void GraphicsDeviceD3D11::Render(const RenderOperation& operation)
	{
		GraphicsDevice::Render(operation);

		ASSERT(operation.material);
		if (!operation.material)
		{
			return;
		}

		// Apply material
		operation.material->Apply(*this, MaterialDomain::Surface, operation.pixelShaderType);
		
		// Determine which vertex shader type to use
		const bool isInstanced = (operation.instanceBuffer != nullptr && operation.instanceCount > 0);
		VertexShaderType vsType = VertexShaderType::Default;
		
		if (isInstanced)
		{
			vsType = VertexShaderType::Instanced;
		}
		else
		{
			const bool hasVertexAnimData = (operation.vertexData->vertexDeclaration->FindElementBySemantic(VertexElementSemantic::BlendIndices) != nullptr);
			vsType = hasVertexAnimData ? VertexShaderType::SkinnedHigh : VertexShaderType::Default;
		}
		
		ShaderBase* vertexShader = operation.material->GetVertexShader(vsType).get();
		if (!vertexShader && vsType != VertexShaderType::Default)
		{
			WLOG("Vertex shader type " << static_cast<int>(vsType) << " not found in material " << operation.material->GetName() << " - falling back to default vertex shader");
			vertexShader = operation.material->GetVertexShader(VertexShaderType::Default).get();
		}

		// By now we should have a vertex shader
		if (vertexShader)
		{
			vertexShader->Set();
		}

		if (!vertexShader)
		{
			return;
		}

		// Bind vertex buffers
		for (const auto& bindings = operation.vertexData->vertexBufferBinding->GetBindings(); const auto & [slot, vertexBuffer] : bindings)
		{
			if (!vertexBuffer)
			{
				continue;
			}

			vertexBuffer->Set(slot);
		}

		// Bind additional constant buffers if any
		int startSlot = 1;
		for (auto& buffer : operation.vertexConstantBuffers)
		{
			ASSERT(buffer);

			ID3D11Buffer* buffers[] = { ((ConstantBufferD3D11*)buffer)->GetBuffer() };
			m_immContext->VSSetConstantBuffers(startSlot++, 1, buffers);
		}

		// Bind additional constant buffers if any
		int psStartSlot = 1;
		for (auto& buffer : operation.pixelConstantBuffers)
		{
			ASSERT(buffer);

			ID3D11Buffer* buffers[] = { ((ConstantBufferD3D11*)buffer)->GetBuffer() };
			m_immContext->PSSetConstantBuffers(psStartSlot++, 1, buffers);
		}
		if (const ConstantBufferPtr scalarBuffer = operation.material->GetParameterBuffer(MaterialParameterType::Scalar, *this))
		{
			scalarBuffer->BindToStage(ShaderType::PixelShader, psStartSlot++);
		}
		if (const ConstantBufferPtr vectorBuffer = operation.material->GetParameterBuffer(MaterialParameterType::Vector, *this))
		{
			vectorBuffer->BindToStage(ShaderType::PixelShader, psStartSlot++);
		}

		SetFaceCullMode(operation.material->IsTwoSided() ? FaceCullMode::None : FaceCullMode::Front);	// ???
		SetBlendMode(operation.material->IsTranslucent() ? BlendMode::Alpha : BlendMode::Opaque);

		// Handle instanced rendering
		if (isInstanced)
		{
			// Determine instance buffer slot (after all vertex buffer slots)
			uint16 instanceSlot = 0;
			for (const auto& bindings = operation.vertexData->vertexBufferBinding->GetBindings(); const auto& [slot, vb] : bindings)
			{
				if (slot >= instanceSlot)
				{
					instanceSlot = slot + 1;
				}
			}

			// Bind instance buffer
			operation.instanceBuffer->Set(instanceSlot);

			// Use instanced input layout
			static_cast<VertexDeclarationD3D11*>(operation.vertexData->vertexDeclaration)->BindInstanced(*static_cast<VertexShaderD3D11*>(vertexShader), operation.vertexData->vertexBufferBinding, instanceSlot);
			SetTopologyType(operation.topology);

			if (operation.indexData)
			{
				operation.indexData->indexBuffer->Set(0);
				DrawIndexedInstanced(
					operation.indexData->indexCount, 
					operation.instanceCount,
					operation.indexData->indexStart,
					0,
					0);
			}
		}
		else
		{
			static_cast<VertexDeclarationD3D11*>(operation.vertexData->vertexDeclaration)->Bind(*static_cast<VertexShaderD3D11*>(vertexShader), operation.vertexData->vertexBufferBinding);
			SetTopologyType(operation.topology);

			if (operation.indexData)
			{
				operation.indexData->indexBuffer->Set(0);
				DrawIndexed(operation.indexData->indexStart, operation.indexData->indexStart + operation.indexData->indexCount);
			}
			else
			{
				Draw(operation.vertexData->vertexCount, operation.vertexData->vertexStart);
			}
		}
	}

	void GraphicsDeviceD3D11::SetHardwareCursor(void* osCursorData)
	{
		m_hardwareCursor = static_cast<HCURSOR>(osCursorData);
	}
	void* GraphicsDeviceD3D11::GetHardwareCursor()
	{
		return m_hardwareCursor;
	}

	std::string GraphicsDeviceD3D11::GetPrimaryMonitorResolution() const
	{
		const int width = GetSystemMetrics(SM_CXSCREEN);
		const int height = GetSystemMetrics(SM_CYSCREEN);
		return std::to_string(width) + "x" + std::to_string(height);
	}

	bool GraphicsDeviceD3D11::ValidateFullscreenResolution(uint16 width, uint16 height) const
	{
		// Get primary monitor resolution
		const int maxWidth = GetSystemMetrics(SM_CXSCREEN);
		const int maxHeight = GetSystemMetrics(SM_CYSCREEN);
		
		// Check if requested resolution is within monitor bounds
		if (width > maxWidth || height > maxHeight)
		{
			WLOG("Requested fullscreen resolution " << width << "x" << height << 
				 " exceeds monitor resolution " << maxWidth << "x" << maxHeight << 
				 ". Using monitor resolution instead.");
			return false;
		}
		
		// For fullscreen, we should use exact monitor resolution for best performance
		if (width != maxWidth || height != maxHeight)
		{
			WLOG("Fullscreen resolution " << width << "x" << height << 
				 " differs from monitor resolution " << maxWidth << "x" << maxHeight << 
				 ". Consider using monitor resolution for optimal performance.");
		}
		
		return true;
	}
}
