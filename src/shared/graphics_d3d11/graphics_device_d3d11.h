// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
	D3D11_MAP MapLockOptionsToD3D11(LockOptions options);

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

		void Clear(ClearFlags flags = ClearFlags::None) override;

		VertexBufferPtr CreateVertexBuffer(size_t vertexCount, size_t vertexSize, BufferUsage usage, const void* initialData = nullptr) override;

		IndexBufferPtr CreateIndexBuffer(size_t indexCount, IndexBufferSize indexSize, BufferUsage usage, const void* initialData = nullptr) override;

		ConstantBufferPtr CreateConstantBuffer(size_t size, const void* initialData) override;

		ShaderPtr CreateShader(ShaderType type, const void* shaderCode, size_t shaderCodeSize) override;

		void UpdateSamplerState();

		void Draw(uint32 vertexCount, uint32 start = 0) override;

		void DrawIndexed(uint32 startIndex = 0, uint32 endIndex = 0) override;

		void SetTopologyType(TopologyType type) override;

		void SetVertexFormat(VertexFormat format) override;

		void SetBlendMode(BlendMode blendMode) override;

		void CaptureState() override;

		void RestoreState() override;

		void SetTransformMatrix(TransformType type, Matrix4 const& matrix) override;

		TexturePtr CreateTexture(uint16 width = 0, uint16 height = 0, BufferUsage usage = BufferUsage::Static) override;

		void BindTexture(TexturePtr texture, ShaderType shader, uint32 slot) override;

		void SetViewport(int32 x, int32 y, int32 w, int32 h, float minZ, float maxZ) override;

		void SetClipRect(int32 x, int32 y, int32 w, int32 h) override;

		void ResetClipRect() override;

		RenderWindowPtr CreateRenderWindow(std::string name, uint16 width, uint16 height, bool fullScreen) override;

		RenderTexturePtr CreateRenderTexture(std::string name, uint16 width, uint16 height) override;

		RenderTexturePtr CreateRenderTexture(std::string name, uint16 width, uint16 height, PixelFormat format) override;

		void SetRenderTargets(RenderTexturePtr* renderTargets, uint32 count) override;

		void SetRenderTargetsWithDepthStencil(RenderTexturePtr* renderTargets, uint32 count, RenderTexturePtr depthStencilRT) override;

		void SetFillMode(FillMode mode) override;

		void SetFaceCullMode(FaceCullMode mode) override;

		void SetTextureAddressMode(TextureAddressMode modeU, TextureAddressMode modeV, TextureAddressMode modeW) override;

		void SetTextureFilter(TextureFilter filter) override;
		
		void SetDepthEnabled(bool enable) override;

		void SetDepthWriteEnabled(bool enable) override;

		void SetDepthTestComparison(DepthTestMethod comparison) override;
		
		std::unique_ptr<MaterialCompiler> CreateMaterialCompiler() override;

		std::unique_ptr<ShaderCompiler> CreateShaderCompiler() override;

		VertexDeclaration* CreateVertexDeclaration() override;

		VertexBufferBinding* CreateVertexBufferBinding() override;

		void Render(const RenderOperation& operation) override;

		void SetHardwareCursor(void* osCursorData) override;

		void* GetHardwareCursor() override;

		uint64 GetBatchCount() const override { return m_lastFrameBatchCount; }
		// ~ End GraphicsDevice

	public:
		void SetIndexCount(const UINT indexCount)
		{
			m_indexCount = indexCount;
		}

		bool HasTearingSupport() const noexcept { return m_tearingSupport; }

		bool IsVSyncEnabled() const noexcept { return m_vsync; }

	public:
		operator ID3D11Device&() const { return *m_device.Get(); }

		operator ID3D11DeviceContext&() const { return *m_immContext.Get(); }

	private:
		/// Checks support for GSync displays.
		void CheckTearingSupport() noexcept;

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

		void UpdateDepthStencilState();
		
		ID3D11SamplerState* GetCurrentSamplerState();

		void UpdateMatrixBuffer();

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
		std::map<size_t, ComPtr<ID3D11DepthStencilState>> m_depthStencilStates;
		/// Constant buffer for vertex shader which contains the matrices.
		ComPtr<ID3D11Buffer> m_matrixBuffer;
		/// Input layouts.
		std::map<VertexFormat, ComPtr<ID3D11InputLayout>> InputLayouts;
		std::map<VertexFormat, ShaderPtr> VertexShaders;
		std::map<VertexFormat, ShaderPtr> PixelShaders;
		VertexFormat m_vertexFormat;
		/// The best supported feature level.
		D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
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
		D3D11_DEPTH_STENCIL_DESC m_depthStencilDesc;
		size_t m_depthStencilHash = 0;
		bool m_depthStencilChanged = false;

#ifdef _DEBUG
		ComPtr<ID3D11Debug> m_d3dDebug;
#endif
		
		Matrix4 m_inverseView;
		Matrix4 m_restoreInverseView;

		Matrix4 m_inverseProj;
		Matrix4 m_restoreInverseProj;

		HCURSOR m_hardwareCursor = nullptr;

		Texture* m_textureSlots[16]{ };
		ShaderBase* m_vertexShader { nullptr };
		ShaderBase* m_pixelShader { nullptr };

		ID3D11InputLayout* m_lastInputLayout{ nullptr };
		uint64 m_batchCount = 0;
		uint64 m_lastFrameBatchCount = 0;
	};
}
