// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "math/matrix4.h"
#include "vertex_buffer.h"
#include "index_buffer.h"
#include "vertex_shader.h"
#include "pixel_shader.h"
#include "vertex_format.h"


namespace mmo
{
	/// Enumerates possible blend modes.
	enum class BlendMode
	{
		/// Opaque rendering. Most performant, default mode.
		Opaque,
		/// Alpha blending enabled.
		Alpha,
	};

	/// Enumerates possible clear flags.
	enum class ClearFlags
	{
		/// Nothing is cleared at all.
		None = 0,
		/// The color buffer is cleared.
		Color = 1,
		/// The depth buffer is cleared.
		Depth = 2,
		/// The stencil buffer is cleared.
		Stencil = 4,

		ColorDepth = Color | Depth,
		All = Color | Depth | Stencil,
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
		static GraphicsDevice& CreateD3D11();
#endif
		/// Gets the current graphics device object. Will throw an exception if there
		/// is no graphics device at the time.
		static GraphicsDevice& Get();
		/// Destroys the current graphics device object if there is one.
		static void Destroy();

	public:
		/// Sets the clear color.
		virtual void SetClearColor(uint32 clearColor);
		/// Called to create the device and do some initialization stuff.
		virtual void Create();
		/// Clears the back buffer as well as the depth buffer.
		virtual void Clear(ClearFlags Flags = ClearFlags::None) = 0;
		/// Presents the back buffer on screen.
		virtual void Present() = 0;
		/// Resizes the graphics device.
		virtual void Resize(uint16 Width, uint16 Height);
		/// Creates a new vertex buffer.
		virtual VertexBufferPtr CreateVertexBuffer(size_t VertexCount, size_t VertexSize, bool dynamic, const void* InitialData = nullptr) = 0;
		/// Creates a new index buffer.
		virtual IndexBufferPtr CreateIndexBuffer(size_t IndexCount, IndexBufferSize IndexSize, const void* InitialData = nullptr) = 0;
		/// Creates a new shader of a certain type if supported.
		virtual ShaderPtr CreateShader(ShaderType Type, const void* ShaderCode, size_t ShaderCodeSize) = 0;
		/// 
		virtual void Draw() = 0;
		/// 
		virtual void DrawIndexed() = 0;
		/// 
		virtual void SetTopologyType(TopologyType InType) = 0;
		/// 
		virtual void SetVertexFormat(VertexFormat InFormat) = 0;
		/// 
		virtual void SetBlendMode(BlendMode InBlendMode) = 0;
		/// Sets the title of the auto created graphics window.
		virtual void SetWindowTitle(const char windowTitle[]) = 0;
		/// Captures the current render state so that it can be restored later on.
		virtual void CaptureState() = 0;
		/// Restores the pushed render state.
		virtual void RestoreState() = 0;
		/// Sets the transform matrix for a certain type of transform.
		virtual void SetTransformMatrix(TransformType type, Matrix4 const& matrix);

	protected:
		Matrix4 m_transform[TransformType::Count];
		uint32 m_clearColor = 0xFF000000;
	};
}
