// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/graphics_device.h"
#include "base/typedefs.h"

#include <map>

// D3D11 header files and namespaces
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace mmo
{
	/// This is the d3d11 implementation of the graphics device class.
	class GraphicsDeviceD3D11 final
		: public GraphicsDevice
	{
	public:
		GraphicsDeviceD3D11();
		~GraphicsDeviceD3D11() override;
		
	public:
		// ~ Begin GraphicsDevice
		Matrix4 MakeProjectionMatrix(const Radian& fovY, float aspect, float nearPlane, float farPlane) override;
		Matrix4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearPlane, float farPlane) override;
		void Reset() override;
		void SetClearColor(uint32 clearColor) override;
		void Create(const GraphicsDeviceDesc& desc) override;
		void Clear(ClearFlags Flags = ClearFlags::None) override;
		VertexBufferPtr CreateVertexBuffer(size_t VertexCount, size_t VertexSize, bool dynamic, const void* InitialData = nullptr) override;
		IndexBufferPtr CreateIndexBuffer(size_t IndexCount, IndexBufferSize IndexSize, const void* InitialData = nullptr) override;
		ShaderPtr CreateShader(ShaderType Type, const void* ShaderCode, size_t ShaderCodeSize) override;
		void UpdateSamplerState();
		void Draw(uint32 vertexCount, uint32 start = 0) override;
		void DrawIndexed() override;
		void SetTopologyType(TopologyType InType) override;
		void SetVertexFormat(VertexFormat InFormat) override;
		void SetBlendMode(BlendMode InBlendMode) override;
		void CaptureState() override;
		void RestoreState() override;
		void SetTransformMatrix(TransformType type, Matrix4 const& matrix) override;
		TexturePtr CreateTexture(uint16 width = 0, uint16 height = 0) override;
		void BindTexture(TexturePtr texture, ShaderType shader, uint32 slot) override;
		void SetViewport(int32 x, int32 y, int32 w, int32 h, float minZ, float maxZ) override;
		void SetClipRect(int32 x, int32 y, int32 w, int32 h) override;
		void ResetClipRect() override;
		RenderWindowPtr CreateRenderWindow(std::string name, uint16 width, uint16 height) override;
		RenderTexturePtr CreateRenderTexture(std::string name, uint16 width, uint16 height) override;
		void SetFillMode(FillMode mode) override;
		void SetFaceCullMode(FaceCullMode mode) override;
		void SetTextureAddressMode(TextureAddressMode modeU, TextureAddressMode modeV, TextureAddressMode modeW) override;
		void SetTextureFilter(TextureFilter filter) override;
		// ~ End GraphicsDevice

	public:
		void SetIndexCount(UINT InIndexCount)
		{
			m_indexCount = InIndexCount;
		}
		bool HasTearingSupport() const noexcept { return m_tearingSupport; }
		bool IsVSyncEnabled() const noexcept { return m_vsync; }

	public:
		operator ID3D11Device&() const { return *m_device.Get(); }
		operator ID3D11DeviceContext&() const { return *m_immContext.Get(); }

	private:
		/// Checks support for GSync displays.
		void CheckTearingSupport();
		/// Creates the d3d11 device objects.
		void CreateD3D11();
		/// Creates the supported input layouts so that we have them loaded right up front.
		void CreateInputLayouts();
		/// Creates all supported blend states.
		void CreateBlendStates();
		/// Creates default constant buffers for internal shaders.
		void CreateConstantBuffers();
		/// Creates the supported rasterizer states.
		void InitRasterizerState();
		/// Initializes the default sampler state desc.
		void InitSamplerState();
		/// Creates the supported sampler state.
		ID3D11SamplerState* CreateSamplerState();
		/// Creates the supported depth states.
		void CreateDepthStates();

		void CreateRasterizerState(bool set = true);

		void UpdateCurrentRasterizerState();
		
		ID3D11SamplerState* GetCurrentSamplerState();

	private:
		/// The d3d11 device object.
		ComPtr<ID3D11Device> m_device;
		/// The immediate context used to generate the final image.
		ComPtr<ID3D11DeviceContext> m_immContext;
		/// Opaque blend state (default).
		ComPtr<ID3D11BlendState> m_opaqueBlendState;
		/// Blend state with alpha blending enabled.
		ComPtr<ID3D11BlendState> m_alphaBlendState;
		/// Rasterizer states.
		std::map<size_t, ComPtr<ID3D11RasterizerState>> m_rasterizerStates;
		/// Sampler states.
		std::map<size_t, ComPtr<ID3D11SamplerState>> m_samplerStates;
		/// Constant buffer for vertex shader which contains the matrices.
		ComPtr<ID3D11Buffer> m_matrixBuffer;
		/// The depth stencil state
		ComPtr<ID3D11DepthStencilState> m_depthStencilState;
		/// Input layouts.
		std::map<VertexFormat, ComPtr<ID3D11InputLayout>> InputLayouts;
		std::map<VertexFormat, ShaderPtr> VertexShaders;
		std::map<VertexFormat, ShaderPtr> PixelShaders;
		/// The best supported feature level.
		D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_9_1;
		/// Whether the device supports GSync displays.
		bool m_tearingSupport = false;
		bool m_matrixDirty = false;
		uint32 m_indexCount = 0;
		FLOAT m_clearColorFloat[4] = {};
		bool m_vsync = true;
		RenderTargetPtr m_renderTarget;
		D3D11_RASTERIZER_DESC m_rasterizerDesc;
		bool m_rasterizerDescChanged = false;
		size_t m_rasterizerHash = 0;
		D3D11_SAMPLER_DESC m_samplerDesc;
		bool m_samplerDescChanged = false;
		size_t m_samplerHash = 0;

#ifdef _DEBUG
		ComPtr<ID3D11Debug> m_d3dDebug;
#endif
	};
}
