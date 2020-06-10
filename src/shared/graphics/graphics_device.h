// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "math/matrix4.h"
#include "vertex_buffer.h"
#include "index_buffer.h"
#include "vertex_shader.h"
#include "pixel_shader.h"
#include "vertex_format.h"
#include "texture.h"
#include "render_window.h"
#include "render_texture.h"


namespace mmo
{
	/// Enumerates supported graphics api's.
	enum class GraphicsApi : uint8
	{
		/// Default value for unknown / unsupported graphics api's. This value is considered an error.
		Unknown,

		/// Direct3D 11. Only on windows 7 and newer.
		D3D11,
		/// OpenGL. Should work on all platforms.
		OpenGL,

		// TODO: Add more graphics api enum values here
	};


	/// This struct describes how a graphics device should be created.
	struct GraphicsDeviceDesc final
	{
		/// If a custom window handle should be used.
		void* customWindowHandle = nullptr;
		uint16 width = 1280;
		uint16 height = 720;
	};


	/// Enumerates possible blend modes.
	enum class BlendMode
	{
		/// Opaque rendering. Most performant, default mode.
		Opaque,
		/// Alpha blending enabled.
		Alpha,
	};


	/// Enumerates primitive topology types.
	enum class TopologyType
	{
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
#ifdef _WIN32
		/// Creates a new d3d11 graphics device object and makes it the current one.
		static GraphicsDevice& CreateD3D11(const GraphicsDeviceDesc& desc);
#endif
		/// Gets the current graphics device object. Will throw an exception if there
		/// is no graphics device at the time.
		static GraphicsDevice& Get();
		/// Destroys the current graphics device object if there is one.
		static void Destroy();

	public:
		/// Resets the graphics device for a new frame.
		virtual void Reset() = 0;
		/// Sets the clear color.
		virtual void SetClearColor(uint32 clearColor);
		/// Called to create the device and do some initialization stuff.
		virtual void Create(const GraphicsDeviceDesc& desc);
		/// Clears the back buffer as well as the depth buffer.
		virtual void Clear(ClearFlags Flags = ClearFlags::None) = 0;
		/// Creates a new vertex buffer.
		virtual VertexBufferPtr CreateVertexBuffer(size_t VertexCount, size_t VertexSize, bool dynamic, const void* InitialData = nullptr) = 0;
		/// Creates a new index buffer.
		virtual IndexBufferPtr CreateIndexBuffer(size_t IndexCount, IndexBufferSize IndexSize, const void* InitialData = nullptr) = 0;
		/// Creates a new shader of a certain type if supported.
		virtual ShaderPtr CreateShader(ShaderType Type, const void* ShaderCode, size_t ShaderCodeSize) = 0;
		/// 
		virtual void Draw(uint32 vertexCount, uint32 start = 0) = 0;
		/// 
		virtual void DrawIndexed() = 0;
		/// 
		virtual void SetTopologyType(TopologyType InType) = 0;
		/// 
		virtual void SetVertexFormat(VertexFormat InFormat) = 0;
		/// 
		virtual void SetBlendMode(BlendMode InBlendMode) = 0;
		/// Captures the current render state so that it can be restored later on.
		virtual void CaptureState();
		/// Restores the pushed render state.
		virtual void RestoreState();
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
		virtual RenderWindowPtr CreateRenderWindow(std::string name, uint16 width, uint16 height) = 0;
		/// Creates a new render texture.
		virtual RenderTexturePtr CreateRenderTexture(std::string name, uint16 width, uint16 height) = 0;

	public:
		inline RenderWindowPtr GetAutoCreatedWindow() const { return m_autoCreatedWindow; }

		inline void RenderTargetActivated(RenderTargetPtr target) { m_renderTarget = target; }

	protected:
		Matrix4 m_transform[TransformType::Count];
		Matrix4 m_restoreTransforms[TransformType::Count];
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
	};
}
