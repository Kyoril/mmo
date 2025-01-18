// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graphics_device_null.h"
#include "index_buffer_null.h"
#include "pixel_shader_null.h"
#include "render_texture_null.h"
#include "render_window_null.h"
#include "texture_null.h"
#include "vertex_buffer_null.h"
#include "vertex_shader_null.h"
#include "shader_compiler_null.h"
#include "material_compiler_null.h"

#include "base/macros.h"

namespace mmo
{
	GraphicsDeviceNull::GraphicsDeviceNull()
	= default;

	GraphicsDeviceNull::~GraphicsDeviceNull()
	= default;

	Matrix4 GraphicsDeviceNull::MakeProjectionMatrix(const Radian& fovY, float aspect, float nearPlane, float farPlane)
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

	Matrix4 GraphicsDeviceNull::MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearPlane, float farPlane)
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

	void GraphicsDeviceNull::Reset()
	{
	}

	void GraphicsDeviceNull::SetClearColor(const uint32 clearColor)
	{
		// Set clear color value in base device
		GraphicsDevice::SetClearColor(clearColor);
	}

	void GraphicsDeviceNull::Create(const GraphicsDeviceDesc& desc)
	{
		// Create the device
		GraphicsDevice::Create(desc);
		
		// Create an automatic render window if requested
		if (desc.customWindowHandle == nullptr)
		{
			m_autoCreatedWindow = CreateRenderWindow("__auto_window__", desc.width, desc.height, !desc.windowed);
		}
		else
		{
			m_autoCreatedWindow = CreateRenderWindow("__auto_window__", desc.width, desc.height, !desc.windowed);
		}
	}

	void GraphicsDeviceNull::Clear(ClearFlags flags)
	{
	}

	VertexBufferPtr GraphicsDeviceNull::CreateVertexBuffer(size_t VertexCount, size_t VertexSize, BufferUsage usage, const void * InitialData)
	{
		return std::make_shared<VertexBufferNull>(*this, VertexCount, VertexSize, usage, InitialData);
	}

	IndexBufferPtr GraphicsDeviceNull::CreateIndexBuffer(size_t IndexCount, IndexBufferSize IndexSize, BufferUsage usage, const void * InitialData)
	{
		return std::make_shared<IndexBufferNull>(*this, IndexCount, IndexSize, usage, InitialData);
	}

	ShaderPtr GraphicsDeviceNull::CreateShader(const ShaderType type, const void * shaderCode, size_t shaderCodeSize)
	{
		switch (type)
		{
		case ShaderType::VertexShader:
			return std::make_unique<VertexShaderNull>(*this, shaderCode, shaderCodeSize);
		case ShaderType::PixelShader:
			return std::make_unique<PixelShaderNull>(*this, shaderCode, shaderCodeSize);
		default:
			ASSERT(! "This shader type can't yet be created - implement it!");
		}

		return nullptr;
	}
	
	void GraphicsDeviceNull::Draw(uint32 vertexCount, uint32 start)
	{
	}

	void GraphicsDeviceNull::DrawIndexed(const uint32 startIndex, const uint32 endIndex)
	{
	}
	
	void GraphicsDeviceNull::SetTopologyType(TopologyType InType)
	{
		GraphicsDevice::SetTopologyType(InType);


	}

	void GraphicsDeviceNull::SetVertexFormat(VertexFormat InFormat)
	{
	}

	void GraphicsDeviceNull::SetBlendMode(BlendMode InBlendMode)
	{
		GraphicsDevice::SetBlendMode(InBlendMode);
		
	}

	void GraphicsDeviceNull::CaptureState()
	{
		GraphicsDevice::CaptureState();
	}

	void GraphicsDeviceNull::RestoreState()
	{
		GraphicsDevice::RestoreState();
	}

	void GraphicsDeviceNull::SetTransformMatrix(TransformType type, Matrix4 const & matrix)
	{
		// Change the transform values
		GraphicsDevice::SetTransformMatrix(type, matrix);
	}

	TexturePtr GraphicsDeviceNull::CreateTexture(uint16 width, uint16 height, BufferUsage usage)
	{
		return std::make_shared<TextureNull>(*this, width, height);
	}

	void GraphicsDeviceNull::BindTexture(TexturePtr texture, ShaderType shader, uint32 slot)
	{

	}

	void GraphicsDeviceNull::SetViewport(int32 x, int32 y, int32 w, int32 h, float minZ, float maxZ)
	{
		// Call super class method
		GraphicsDevice::SetViewport(x, y, w, h, minZ, maxZ);
		
	}

	void GraphicsDeviceNull::SetClipRect(int32 x, int32 y, int32 w, int32 h)
	{

	}

	void GraphicsDeviceNull::ResetClipRect()
	{

	}

	RenderWindowPtr GraphicsDeviceNull::CreateRenderWindow(std::string name, uint16 width, uint16 height, bool fullScreen)
	{
		return std::make_shared<RenderWindowNull>(*this, std::move(name), width, height, fullScreen);
	}

	RenderTexturePtr GraphicsDeviceNull::CreateRenderTexture(std::string name, uint16 width, uint16 height)
	{
		return std::make_shared<RenderTextureNull>(*this, std::move(name), width, height);
	}

	void GraphicsDeviceNull::SetFillMode(FillMode mode)
	{
		GraphicsDevice::SetFillMode(mode);
	}

	void GraphicsDeviceNull::SetFaceCullMode(FaceCullMode mode)
	{
		GraphicsDevice::SetFaceCullMode(mode);
	}

	void GraphicsDeviceNull::SetTextureAddressMode(const TextureAddressMode modeU, const TextureAddressMode modeV, const TextureAddressMode modeW)
	{
		GraphicsDevice::SetTextureAddressMode(modeU, modeV, modeW);
		
	}
	
	void GraphicsDeviceNull::SetTextureFilter(const TextureFilter filter)
	{
		GraphicsDevice::SetTextureFilter(filter);
		
	}

	void GraphicsDeviceNull::SetDepthEnabled(const bool enable)
	{
		GraphicsDevice::SetDepthEnabled(enable);
		
	}

	void GraphicsDeviceNull::SetDepthWriteEnabled(const bool enable)
	{
		GraphicsDevice::SetDepthWriteEnabled(enable);
		
	}
	
	void GraphicsDeviceNull::SetDepthTestComparison(const DepthTestMethod comparison)
	{
		GraphicsDevice::SetDepthTestComparison(comparison);
		
	}

	std::unique_ptr<MaterialCompiler> GraphicsDeviceNull::CreateMaterialCompiler()
	{
		return std::make_unique<MaterialCompilerNull>();
	}

	std::unique_ptr<ShaderCompiler> GraphicsDeviceNull::CreateShaderCompiler()
	{
		return std::make_unique<ShaderCompilerNull>();
	}

	ConstantBufferPtr GraphicsDeviceNull::CreateConstantBuffer(size_t size, const void* initialData)
	{
		return nullptr;
	}

	void GraphicsDeviceNull::SetHardwareCursor(void* osCursorData)
	{
	}

	void* GraphicsDeviceNull::GetHardwareCursor()
	{
		return nullptr;
	}
}
