// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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
		// ~ Begin GraphicsDevice
		virtual Matrix4 MakeProjectionMatrix(const Radian& fovY, float aspect, float nearPlane, float farPlane) final override;
		virtual Matrix4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearPlane, float farPlane) final override;
		virtual void Reset() final override;
		virtual void SetClearColor(uint32 clearColor) final override;
		virtual void Create(const GraphicsDeviceDesc& desc) final override;
		virtual void Clear(ClearFlags Flags = ClearFlags::None) final override;
		virtual VertexBufferPtr CreateVertexBuffer(size_t VertexCount, size_t VertexSize, bool dynamic, const void* InitialData = nullptr) final override;
		virtual IndexBufferPtr CreateIndexBuffer(size_t IndexCount, IndexBufferSize IndexSize, const void* InitialData = nullptr) final override;
		virtual ShaderPtr CreateShader(ShaderType Type, const void* ShaderCode, size_t ShaderCodeSize) final override;
		virtual void Draw(uint32 vertexCount, uint32 start = 0) final override;
		virtual void DrawIndexed() final override;
		virtual void SetTopologyType(TopologyType InType) final override;
		virtual void SetVertexFormat(VertexFormat InFormat) final override;
		virtual void SetBlendMode(BlendMode InBlendMode) final override;
		virtual void CaptureState() final override;
		virtual void RestoreState() final override;
		virtual void SetTransformMatrix(TransformType type, Matrix4 const& matrix) final override;
		virtual TexturePtr CreateTexture(uint16 width = 0, uint16 height = 0) final override;
		virtual void BindTexture(TexturePtr texture, ShaderType shader, uint32 slot) final override;
		virtual void SetViewport(int32 x, int32 y, int32 w, int32 h, float minZ, float maxZ) final override;
		virtual void SetClipRect(int32 x, int32 y, int32 w, int32 h) final override;
		virtual void ResetClipRect() final override;
		virtual RenderWindowPtr CreateRenderWindow(std::string name, uint16 width, uint16 height) final override;
		virtual RenderTexturePtr CreateRenderTexture(std::string name, uint16 width, uint16 height) final override;
		virtual void SetFillMode(FillMode mode) final override;
		virtual void SetFaceCullMode(FaceCullMode mode) final override;
		// ~ End GraphicsDevice

	public:
		inline void SetIndexCount(UINT InIndexCount)
		{
			m_indexCount = InIndexCount;
		}
		inline bool HasTearingSupport() const noexcept { return m_tearingSupport; }
		inline bool IsVSyncEnabled() const noexcept { return m_vsync; }

	public:
		inline operator ID3D11Device&() const { return *m_device.Get(); }
		inline operator ID3D11DeviceContext&() const { return *m_immContext.Get(); }

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
		/// Creates the supported sampler states.
		void CreateSamplerStates();
		/// Creates the supported depth states.
		void CreateDepthStates();

		void CreateRasterizerState(bool set = true);

		void UpdateCurrentRasterizerState();

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
		/// Constant buffer for vertex shader which contains the matrices.
		ComPtr<ID3D11Buffer> m_matrixBuffer;
		/// The default texture sampler.
		ComPtr<ID3D11SamplerState> m_samplerState;
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
		FLOAT m_clearColorFloat[4];
		bool m_vsync = true;
		RenderTargetPtr m_renderTarget;
		D3D11_RASTERIZER_DESC m_rasterizerDesc;
		bool m_rasterizerDescChanged = false;
		size_t m_rasterizerHash = 0;
	};
}
