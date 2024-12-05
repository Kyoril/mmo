// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/graphics_device.h"
#include "base/typedefs.h"

#include "Metal/Metal.hpp"

namespace mmo
{
	/// This is the null implementation of the graphics device class.
    class GraphicsDeviceMetal final
		: public GraphicsDevice
	{
	public:
        GraphicsDeviceMetal();

		~GraphicsDeviceMetal() override;
		
	public:
        void SetHardwareCursor(void* osCursorData) override;

        void* GetHardwareCursor() override;

		// ~ Begin GraphicsDevice
		Matrix4 MakeProjectionMatrix(const Radian& fovY, float aspect, float nearPlane, float farPlane) override;

		Matrix4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearPlane, float farPlane) override;

		void Reset() override;

		void SetClearColor(uint32 clearColor) override;

		void Create(const GraphicsDeviceDesc& desc) override;

		void Clear(ClearFlags Flags = ClearFlags::None) override;

        ConstantBufferPtr CreateConstantBuffer(size_t size, const void* initialData = nullptr) override;

        VertexBufferPtr CreateVertexBuffer(size_t vertexCount, size_t vertexSize, BufferUsage usage, const void* initialData = nullptr) override;

        IndexBufferPtr CreateIndexBuffer(size_t indexCount, IndexBufferSize indexSize, BufferUsage usage, const void* initialData = nullptr) override;
        
		ShaderPtr CreateShader(ShaderType type, const void* shaderCode, size_t shaderCodeSize) override;

		void Draw(uint32 vertexCount, uint32 start = 0) override;

		void DrawIndexed(uint32 startIndex = 0, uint32 endIndex = 0) override;

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

		RenderWindowPtr CreateRenderWindow(std::string name, uint16 width, uint16 height, bool fullScreen) override;

		RenderTexturePtr CreateRenderTexture(std::string name, uint16 width, uint16 height) override;

		void SetFillMode(FillMode mode) override;

		void SetFaceCullMode(FaceCullMode mode) override;

		void SetTextureAddressMode(TextureAddressMode modeU, TextureAddressMode modeV, TextureAddressMode modeW) override;

		void SetTextureFilter(TextureFilter filter) override;

		void SetDepthEnabled(bool enable) override;

		void SetDepthWriteEnabled(bool enable) override;

		void SetDepthTestComparison(DepthTestMethod comparison) override;

		std::unique_ptr<MaterialCompiler> CreateMaterialCompiler() override;

		std::unique_ptr<ShaderCompiler> CreateShaderCompiler() override;
		// ~ End GraphicsDevice
        
    public:
        MTL::Device* GetDevice() { return m_device; }
        MTL::CommandQueue* GetCommandQueue() { return m_commandQueue; }
        
    private:
        MTL::Device* m_device;
        MTL::CommandQueue* m_commandQueue;
	};
}
