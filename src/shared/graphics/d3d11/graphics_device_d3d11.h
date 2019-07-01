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
	class GraphicsDeviceD3D11
		: public GraphicsDevice
	{
	public:
		// ~ Begin GraphicsDevice
		virtual void SetClearColor(uint32 clearColor) override;
		virtual void Create() override;
		virtual void Clear(ClearFlags Flags = ClearFlags::None) override;
		virtual void Present() override;
		virtual void Resize(uint16 Width, uint16 Height) override;
		virtual VertexBufferPtr CreateVertexBuffer(size_t VertexCount, size_t VertexSize, bool dynamic, const void* InitialData = nullptr) override;
		virtual IndexBufferPtr CreateIndexBuffer(size_t IndexCount, IndexBufferSize IndexSize, const void* InitialData = nullptr) override;
		virtual ShaderPtr CreateShader(ShaderType Type, const void* ShaderCode, size_t ShaderCodeSize) override;
		virtual void Draw(uint32 vertexCount, uint32 start = 0) override;
		virtual void DrawIndexed() override;
		virtual void SetTopologyType(TopologyType InType) override;
		virtual void SetVertexFormat(VertexFormat InFormat) override;
		virtual void SetBlendMode(BlendMode InBlendMode) override;
		virtual void SetWindowTitle(const char windowTitle[]) override;
		virtual void CaptureState() override;
		virtual void RestoreState() override;
		virtual void SetTransformMatrix(TransformType type, Matrix4 const& matrix) override;
		virtual TexturePtr CreateTexture(uint16 width = 0, uint16 height = 0) override;
		virtual void BindTexture(TexturePtr texture, ShaderType shader, uint32 slot) override;
		// ~ End GraphicsDevice

	public:
		inline void SetIndexCount(UINT InIndexCount)
		{
			m_indexCount = InIndexCount;
		}

	public:
		inline operator ID3D11Device&() const { return *m_device.Get(); }
		inline operator ID3D11DeviceContext&() const { return *m_immContext.Get(); }
		inline operator IDXGISwapChain&() const { return *m_swapChain.Get(); }
		inline operator ID3D11RenderTargetView&() const { return *m_renderTargetView.Get(); }

	private:
		/// Ensures that the internal window class is created.
		void EnsureWindowClassCreated();
		/// Checks support for GSync displays.
		void CheckTearingSupport();
		/// Creates an internal render window.
		void CreateInternalWindow(uint16 width, uint16 height);
		/// Creates the d3d11 device objects.
		void CreateD3D11();
		/// Creates resources that might be recreated if the buffer sizes change.
		void CreateSizeDependantResources();
		/// Creates the supported input layouts so that we have them loaded right up front.
		void CreateInputLayouts();
		/// Creates all supported blend states.
		void CreateBlendStates();
		/// Creates default constant buffers for internal shaders.
		void CreateConstantBuffers();
		/// Creates the supported rasterizer states.
		void CreateRasterizerStates();
		/// Creates the supported sampler states.
		void CreateSamplerStates();
		/// Creates the supported depth states.
		void CreateDepthStates();

	private:
		/// The render window callback procedure for internally created windows.
		static LRESULT RenderWindowProc(HWND Wnd, UINT Msg, WPARAM WParam, LPARAM LParam);


	private:
		/// The d3d11 device object.
		ComPtr<ID3D11Device> m_device;
		/// The immediate context used to generate the final image.
		ComPtr<ID3D11DeviceContext> m_immContext;
		/// The swap chain.
		ComPtr<IDXGISwapChain> m_swapChain;
		/// Render target view.
		ComPtr<ID3D11RenderTargetView> m_renderTargetView;
		/// Depth and stencil buffer view.
		ComPtr<ID3D11DepthStencilView> m_depthStencilView;
		/// Opaque blend state (default).
		ComPtr<ID3D11BlendState> m_opaqueBlendState;
		/// Blend state with alpha blending enabled.
		ComPtr<ID3D11BlendState> m_alphaBlendState;
		/// Default rasterizer state.
		ComPtr<ID3D11RasterizerState> m_rasterizerState;
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
		D3D_FEATURE_LEVEL m_featureLevel;
		/// The window handle.
		HWND m_windowHandle = nullptr;
		/// Whether it's our own window (which we need to destroy ourself).
		bool m_ownWindow = false;
		/// Whether the device supports GSync displays.
		bool m_tearingSupport = false;

		uint16 m_width = 1024;

		uint16 m_height = 768;

		bool m_resizePending = false;

		bool m_matrixDirty = false;

		uint32 m_indexCount = 0;

		FLOAT m_clearColorFloat[4];
	};
}
