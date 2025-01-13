// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "math/matrix4.h"
#include "vertex_buffer.h"
#include "index_buffer.h"
#include "pixel_shader.h"
#include "vertex_format.h"
#include "texture.h"
#include "render_window.h"
#include "render_texture.h"
#include "material_compiler.h"
#include "vertex_declaration.h"
#include "shared/graphics/constant_buffer.h"


namespace mmo
{
	class RenderOperation;
}

namespace mmo
{
	class Radian;

	/// Enumerates supported graphics api's.
	enum class GraphicsApi : uint8
	{
		/// Default value for unknown / unsupported graphics api's. This value is considered an error.
		Unknown,

		/// Direct3D 11. Only on windows 7 and newer.
		D3D11,

		/// OpenGL. Should work on all platforms.
		OpenGL,
		
        /// Metal API for MacOS.
        Metal,
        
		/// Null. Should work on every platform but not render anything at all.
		Null,

		// TODO: Add more graphics api enum values here
	};


	/// This struct describes how a graphics device should be created.
	struct GraphicsDeviceDesc final
	{
		/// If a custom window handle should be used.
		void* customWindowHandle = nullptr;
		uint16 width = 1280;
		uint16 height = 720;
		bool vsync = true;
		bool windowed = true;
	};


	/// Enumerates possible blend modes.
	enum class BlendMode
	{
		Undefined,

		/// Opaque rendering. Most performant, default mode.
		Opaque,

		/// Alpha blending enabled.
		Alpha,
	};


	/// Enumerates primitive topology types.
	enum class TopologyType
	{
		Undefined,

		PointList,
		LineList,
		LineStrip,
		TriangleList,
		TriangleStrip
	};


	enum TransformType
	{
		World,
		View,
		Projection,

		Count
	};

	/// Enumerates possible polygon fill modes.
	enum class FillMode
	{
		/// Polygons will be rendered solid.
		Solid,

		/// Only the edges of polygons will be rendered.
		Wireframe,
	};

	/// Enumerates possible face cull modes.
	enum class FaceCullMode
	{
		/// No front- or backface culling enabled.
		None,

		/// Back-facing polygons will be culled.
		Back,

		/// Front-facing polygons will be culled.
		Front,
	};

	enum class DepthTestMethod
	{
		Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
	};

	/// This is the base class of a graphics device object.
	class GraphicsDevice
		: public NonCopyable
	{
	protected:
		// Protected default constructor.
		GraphicsDevice();

	public:
		/// Virtual default destructor.
		virtual ~GraphicsDevice() = default;

	public:
		/// Creates a new null graphics device object and makes it the current one.
		static GraphicsDevice& CreateNull(const GraphicsDeviceDesc& desc);

#ifdef _WIN32
		/// Creates a new d3d11 graphics device object and makes it the current one.
		static GraphicsDevice& CreateD3D11(const GraphicsDeviceDesc& desc);
#endif
        
#ifdef __APPLE__
        static GraphicsDevice& CreateMetal(const GraphicsDeviceDesc& desc);
#endif

		/// Gets the current graphics device object. Will throw an exception if there
		/// is no graphics device at the time.
		static GraphicsDevice& Get();

		/// Destroys the current graphics device object if there is one.
		static void Destroy();

	public:
		virtual void SetHardwareCursor(void* osCursorData) = 0;

		virtual void* GetHardwareCursor() = 0;

		/// Resets the graphics device for a new frame.
		virtual void Reset() = 0;

		/// Sets the clear color.
		virtual void SetClearColor(uint32 clearColor);

		/// Called to create the device and do some initialization stuff.
		virtual void Create(const GraphicsDeviceDesc& desc);

		/// Clears the back buffer as well as the depth buffer.
		virtual void Clear(ClearFlags flags = ClearFlags::None) = 0;

		/// Creates a new vertex buffer.
		virtual VertexBufferPtr CreateVertexBuffer(size_t vertexCount, size_t vertexSize, BufferUsage usage, const void* initialData = nullptr) = 0;

		/// Creates a new index buffer.
		virtual IndexBufferPtr CreateIndexBuffer(size_t indexCount, IndexBufferSize indexSize, BufferUsage usage, const void* initialData = nullptr) = 0;

		/// Creates a new constant buffer.
		virtual ConstantBufferPtr CreateConstantBuffer(size_t size, const void* initialData = nullptr) = 0;

		/// Creates a new shader of a certain type if supported.
		virtual ShaderPtr CreateShader(ShaderType type, const void* shaderCode, size_t shaderCodeSize) = 0;

		virtual void Render(const RenderOperation& operation) {}

		/// 
		virtual void Draw(uint32 vertexCount, uint32 start = 0) = 0;

		/// 
		virtual void DrawIndexed(uint32 startIndex = 0, uint32 endIndex = 0) = 0;

		/// 
		virtual void SetTopologyType(TopologyType type);

		/// 
		virtual void SetVertexFormat(VertexFormat format) = 0;

		/// 
		virtual void SetBlendMode(BlendMode blendMode);

		/// Captures the current render state so that it can be restored later on.
		virtual void CaptureState();

		/// Restores the pushed render state.
		virtual void RestoreState();

		/// Creates a projection matrix for this render api.
		virtual Matrix4 MakeProjectionMatrix(const Radian& fovY, float aspect, float nearPlane, float farPlane) = 0;

		/// Creates an orthographic projection matrix for this render api.
		virtual Matrix4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearPlane, float farPlane) = 0;

		/// Gets the transform matrix for a certain type of transform.
		virtual Matrix4 GetTransformMatrix(TransformType type) const;

		/// Sets the transform matrix for a certain type of transform.
		virtual void SetTransformMatrix(TransformType type, Matrix4 const& matrix);

		/// Create a new texture object.
		virtual TexturePtr CreateTexture(uint16 width = 0, uint16 height = 0) = 0;

		/// Binds a texture to the given slot for a given shader type.
		virtual void BindTexture(TexturePtr texture, ShaderType shader, uint32 slot) = 0;

		/// Gets the current viewport dimensions.
		virtual void GetViewport(int32* x = nullptr, int32* y = nullptr, int32* w = nullptr, int32* h = nullptr, float* minZ = nullptr, float* maxZ = nullptr);

		/// Sets the current viewport dimensions.
		virtual void SetViewport(int32 x, int32 y, int32 w, int32 h, float minZ, float maxZ);

		/// Sets the clipping rect.
		virtual void SetClipRect(int32 x, int32 y, int32 w, int32 h) = 0;

		/// Resets the clipping rect if there is any.
		virtual void ResetClipRect() = 0;

		/// Creates a new render window.
		virtual RenderWindowPtr CreateRenderWindow(std::string name, uint16 width, uint16 height, bool fullScreen) = 0;

		/// Creates a new render texture.
		virtual RenderTexturePtr CreateRenderTexture(std::string name, uint16 width, uint16 height) = 0;

		/// Sets the fill mode of polygons.
		virtual void SetFillMode(FillMode mode);

		/// Sets the cull mode of polygons.
		virtual void SetFaceCullMode(FaceCullMode mode);

		/// Sets the texture address mode used when sampling textures.
		virtual void SetTextureAddressMode(const TextureAddressMode mode) { SetTextureAddressMode(mode, mode, mode); }

		/// Sets the texture address mode used when sampling textures.
		virtual void SetTextureAddressMode(TextureAddressMode modeU, TextureAddressMode modeV, TextureAddressMode modeW);

		/// Sets the texture address mode used when sampling textures.
		void SetTextureAddressModeU(const TextureAddressMode mode) { SetTextureAddressMode(mode, m_texAddressMode[1], m_texAddressMode[2]); }

		/// Sets the texture address mode used when sampling textures.
		void SetTextureAddressModeV(const TextureAddressMode mode) { SetTextureAddressMode(m_texAddressMode[0], mode, m_texAddressMode[2]); }

		/// Sets the texture address mode used when sampling textures.
		void SetTextureAddressModeW(const TextureAddressMode mode)  { SetTextureAddressMode(m_texAddressMode[0], m_texAddressMode[1], mode); }

		/// Sets the texture filter to be used when sampling textures.
		virtual void SetTextureFilter(TextureFilter filter);

		virtual void SetDepthEnabled(bool enable);

		virtual void SetDepthWriteEnabled(bool enable);

		virtual void SetDepthTestComparison(DepthTestMethod comparison);

		virtual std::unique_ptr<MaterialCompiler> CreateMaterialCompiler() = 0;

		virtual std::unique_ptr<ShaderCompiler> CreateShaderCompiler() = 0;

		virtual VertexDeclaration* CreateVertexDeclaration();

		virtual void DestroyVertexDeclaration(VertexDeclaration& declaration);

		virtual VertexBufferBinding* CreateVertexBufferBinding();

		virtual void DestroyVertexBufferBinding(VertexBufferBinding& binding);

		virtual uint64 GetBatchCount() const = 0;

	public:
		RenderWindowPtr GetAutoCreatedWindow() const { return m_autoCreatedWindow; }

		void RenderTargetActivated(const RenderTargetPtr target) { m_renderTarget = target; }

	protected:
		Matrix4 m_transform[Count];
		Matrix4 m_restoreTransforms[Count];
		uint32 m_clearColor = 0xFF000000;
		int32 m_viewX;
		int32 m_viewY;
		int32 m_viewW;
		int32 m_viewH;
		float m_viewMinZ;
		float m_viewMaxZ;
		RenderWindowPtr m_autoCreatedWindow;
		RenderTargetPtr m_renderTarget;
		RenderTargetPtr m_restoreRenderTarget;
		TopologyType m_topologyType;
		TopologyType m_captureTopologyType;
		BlendMode m_blendMode;
		BlendMode m_restoreBlendMode;
		FillMode m_fillMode;
		FillMode m_restoreFillMode;
		FaceCullMode m_cullMode { FaceCullMode::Front };
		FaceCullMode m_restoreCullMode { FaceCullMode::Front };
		TextureAddressMode m_texAddressMode[3];
		TextureAddressMode m_restoreTexAddressMode[3];
		TextureFilter m_texFilter;
		TextureFilter m_restoreTexFilter;
		bool m_depthEnabled { false };
		bool m_restoreDepthEnable { false };
		bool m_depthWrite { false };
		bool m_restoreDepthWrite { false };
		DepthTestMethod m_depthComparison { DepthTestMethod::Always };
		DepthTestMethod m_restoreDepthComparison { DepthTestMethod::Always };
		std::vector<std::unique_ptr<VertexDeclaration>> m_vertexDeclarations;
		std::vector<std::unique_ptr<VertexBufferBinding>> m_vertexBufferBindings;
	};
}
