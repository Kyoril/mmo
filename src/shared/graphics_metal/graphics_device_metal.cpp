// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "graphics_device_metal.h"
#include "index_buffer_metal.h"
#include "pixel_shader_metal.h"
#include "render_texture_metal.h"
#include "render_window_metal.h"
#include "texture_metal.h"
#include "vertex_buffer_metal.h"
#include "vertex_shader_metal.h"
#include "shader_compiler_metal.h"
#include "material_compiler_metal.h"

#include "base/macros.h"

namespace mmo
{
    GraphicsDeviceMetal::GraphicsDeviceMetal()
	= default;

    GraphicsDeviceMetal::~GraphicsDeviceMetal()
	= default;

	Matrix4 GraphicsDeviceMetal::MakeProjectionMatrix(const Radian& fovY, float aspect, float nearPlane, float farPlane)
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

	Matrix4 GraphicsDeviceMetal::MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearPlane, float farPlane)
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

	void GraphicsDeviceMetal::Reset()
	{
	}

	void GraphicsDeviceMetal::SetClearColor(const uint32 clearColor)
	{
		// Set clear color value in base device
		GraphicsDevice::SetClearColor(clearColor);
	}

	void GraphicsDeviceMetal::Create(const GraphicsDeviceDesc& desc)
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

	void GraphicsDeviceMetal::Clear(ClearFlags flags)
	{
	}

	VertexBufferPtr GraphicsDeviceMetal::CreateVertexBuffer(size_t VertexCount, size_t VertexSize, bool dynamic, const void * InitialData)
	{
		return std::make_unique<VertexBufferMetal>(*this, VertexCount, VertexSize, dynamic, InitialData);
	}

	IndexBufferPtr GraphicsDeviceMetal::CreateIndexBuffer(size_t IndexCount, IndexBufferSize IndexSize, const void * InitialData)
	{
		return std::make_unique<IndexBufferMetal>(*this, IndexCount, IndexSize, InitialData);
	}

	ShaderPtr GraphicsDeviceMetal::CreateShader(const ShaderType type, const void * shaderCode, size_t shaderCodeSize)
	{
		switch (type)
		{
		case ShaderType::VertexShader:
			return std::make_unique<VertexShaderMetal>(*this, shaderCode, shaderCodeSize);
		case ShaderType::PixelShader:
			return std::make_unique<PixelShaderMetal>(*this, shaderCode, shaderCodeSize);
		default:
			ASSERT(! "This shader type can't yet be created - implement it!");
		}

		return nullptr;
	}
	
	void GraphicsDeviceMetal::Draw(uint32 vertexCount, uint32 start)
	{
	}

	void GraphicsDeviceMetal::DrawIndexed(const uint32 startIndex, const uint32 endIndex)
	{
	}
	
	void GraphicsDeviceMetal::SetTopologyType(TopologyType InType)
	{
		GraphicsDevice::SetTopologyType(InType);


	}

	void GraphicsDeviceMetal::SetVertexFormat(VertexFormat InFormat)
	{
	}

	void GraphicsDeviceMetal::SetBlendMode(BlendMode InBlendMode)
	{
		GraphicsDevice::SetBlendMode(InBlendMode);
		
	}

	void GraphicsDeviceMetal::CaptureState()
	{
		GraphicsDevice::CaptureState();
	}

	void GraphicsDeviceMetal::RestoreState()
	{
		GraphicsDevice::RestoreState();
	}

	void GraphicsDeviceMetal::SetTransformMatrix(TransformType type, Matrix4 const & matrix)
	{
		// Change the transform values
		GraphicsDevice::SetTransformMatrix(type, matrix);
	}

	TexturePtr GraphicsDeviceMetal::CreateTexture(uint16 width, uint16 height)
	{
		return std::make_shared<TextureMetal>(*this, width, height);
	}

	void GraphicsDeviceMetal::BindTexture(TexturePtr texture, ShaderType shader, uint32 slot)
	{

	}

	void GraphicsDeviceMetal::SetViewport(int32 x, int32 y, int32 w, int32 h, float minZ, float maxZ)
	{
		// Call super class method
		GraphicsDevice::SetViewport(x, y, w, h, minZ, maxZ);
		
	}

	void GraphicsDeviceMetal::SetClipRect(int32 x, int32 y, int32 w, int32 h)
	{

	}

	void GraphicsDeviceMetal::ResetClipRect()
	{

	}

	RenderWindowPtr GraphicsDeviceMetal::CreateRenderWindow(std::string name, uint16 width, uint16 height, bool fullScreen)
	{
		return std::make_shared<RenderWindowMetal>(*this, std::move(name), width, height, fullScreen);
	}

	RenderTexturePtr GraphicsDeviceMetal::CreateRenderTexture(std::string name, uint16 width, uint16 height)
	{
		return std::make_shared<RenderTextureMetal>(*this, std::move(name), width, height);
	}

	void GraphicsDeviceMetal::SetFillMode(FillMode mode)
	{
		GraphicsDevice::SetFillMode(mode);
	}

	void GraphicsDeviceMetal::SetFaceCullMode(FaceCullMode mode)
	{
		GraphicsDevice::SetFaceCullMode(mode);
	}

	void GraphicsDeviceMetal::SetTextureAddressMode(const TextureAddressMode modeU, const TextureAddressMode modeV, const TextureAddressMode modeW)
	{
		GraphicsDevice::SetTextureAddressMode(modeU, modeV, modeW);
		
	}
	
	void GraphicsDeviceMetal::SetTextureFilter(const TextureFilter filter)
	{
		GraphicsDevice::SetTextureFilter(filter);
		
	}

	void GraphicsDeviceMetal::SetDepthEnabled(const bool enable)
	{
		GraphicsDevice::SetDepthEnabled(enable);
		
	}

	void GraphicsDeviceMetal::SetDepthWriteEnabled(const bool enable)
	{
		GraphicsDevice::SetDepthWriteEnabled(enable);
		
	}
	
	void GraphicsDeviceMetal::SetDepthTestComparison(const DepthTestMethod comparison)
	{
		GraphicsDevice::SetDepthTestComparison(comparison);
		
	}

	std::unique_ptr<MaterialCompiler> GraphicsDeviceMetal::CreateMaterialCompiler()
	{
		return std::make_unique<MaterialCompilerMetal>();
	}

	std::unique_ptr<ShaderCompiler> GraphicsDeviceMetal::CreateShaderCompiler()
	{
		return std::make_unique<ShaderCompilerMetal>();
	}
}
