// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics_device_metal.h"

#include "graphics/constant_buffer.h"
#include "graphics/index_buffer.h"
#include "graphics/occlusion_query.h"
#include "graphics/render_texture.h"
#include "graphics/sampler_state.h"
#include "graphics/shader_base.h"
#include "graphics/structured_buffer.h"
#include "graphics/texture.h"
#include "graphics/texture_array.h"
#include "graphics/vertex_buffer.h"
#include "graphics/volume_texture.h"

#include <vector>

namespace mmo
{
	/// @brief A GPU buffer the CPU writes while the GPU may still read older contents.
	/// @remarks Writing a buffer the GPU has not finished reading would corrupt frames in flight, so
	///          every discarding map moves to a buffer instance the GPU no longer uses (renaming), the
	///          way D3D11 drivers handle WRITE_DISCARD.
	class RenamedMetalBuffer final
	{
	public:
		RenamedMetalBuffer(GraphicsDeviceMetal& device, size_t size, const void* initialData);

	public:
		void* Map(LockOptions options);

		/// @brief The buffer instance holding the current contents, marked as used by this frame.
		id<MTLBuffer> Use();

		[[nodiscard]] id<MTLBuffer> Current() const { return m_instances[m_current].buffer; }

		[[nodiscard]] size_t GetSize() const { return m_size; }

	private:
		struct Instance
		{
			id<MTLBuffer> buffer;
			uint64 lastUsedFrame = 0;
		};

		GraphicsDeviceMetal& m_device;
		size_t m_size;
		std::vector<Instance> m_instances;
		size_t m_current = 0;
	};

	class ShaderMetal final : public ShaderBase
	{
	public:
		ShaderMetal(GraphicsDeviceMetal& device, ShaderType type, id<MTLFunction> function, const uint32 threadgroupSize[3]);

		~ShaderMetal() override;

	public:
		void Set() override;

		[[nodiscard]] ShaderType GetType() const override { return m_type; }

		[[nodiscard]] id<MTLFunction> GetFunction() const { return m_function; }

		[[nodiscard]] MTLSize GetThreadgroupSize() const { return m_threadgroupSize; }

	private:
		GraphicsDeviceMetal& m_device;
		ShaderType m_type;
		id<MTLFunction> m_function;
		MTLSize m_threadgroupSize;
	};

	class VertexBufferMetal final : public VertexBuffer
	{
	public:
		VertexBufferMetal(GraphicsDeviceMetal& device, size_t vertexCount, size_t vertexSize, BufferUsage usage, const void* initialData);

		~VertexBufferMetal() override;

	public:
		void* Map(LockOptions lock) override;

		void Unmap() override {}

		void Set(uint16 slot) override;

		VertexBufferPtr Clone() override;

		RenamedMetalBuffer& GetBuffer() { return m_buffer; }

	private:
		GraphicsDeviceMetal& m_device;
		RenamedMetalBuffer m_buffer;
	};

	class IndexBufferMetal final : public IndexBuffer
	{
	public:
		IndexBufferMetal(GraphicsDeviceMetal& device, size_t indexCount, IndexBufferSize indexSize, BufferUsage usage, const void* initialData);

		~IndexBufferMetal() override;

	public:
		void* Map(LockOptions lock) override;

		void Unmap() override {}

		void Set(uint16 slot) override;

		RenamedMetalBuffer& GetBuffer() { return m_buffer; }

		[[nodiscard]] MTLIndexType GetMetalIndexType() const { return IndexSize == IndexBufferSize::Index_16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32; }

		[[nodiscard]] uint32 GetIndexByteSize() const { return IndexSize == IndexBufferSize::Index_16 ? 2 : 4; }

	private:
		GraphicsDeviceMetal& m_device;
		RenamedMetalBuffer m_buffer;
	};

	/// @brief A constant buffer is CPU memory that is copied into the command stream for every draw
	///        it changed for, which gives each draw the contents it saw when it was recorded.
	class ConstantBufferMetal final : public ConstantBuffer
	{
	public:
		ConstantBufferMetal(GraphicsDeviceMetal& device, size_t size, const void* initialData);

		~ConstantBufferMetal() override;

	public:
		void BindToStage(ShaderType type, uint32 slot) override;

		void Update(void* data) override;

		[[nodiscard]] const uint8* GetData() const { return m_data.data(); }

		/// Increases on every update, so the device can tell whether bound contents changed.
		[[nodiscard]] uint64 GetVersion() const { return m_version; }

	private:
		GraphicsDeviceMetal& m_device;
		std::vector<uint8> m_data;
		uint64 m_version = 1;
	};

	class StructuredBufferMetal final : public StructuredBuffer
	{
	public:
		StructuredBufferMetal(GraphicsDeviceMetal& device, size_t elementSize, size_t elementCount, const void* initialData);

	public:
		void BindToStage(ShaderType type, uint32 slot) override;

		void Update(const void* data, size_t elementCount) override;

	private:
		GraphicsDeviceMetal& m_device;
		RenamedMetalBuffer m_buffer;
	};

	class TextureMetal : public Texture
	{
	public:
		TextureMetal(GraphicsDeviceMetal& device, uint16 width, uint16 height, BufferUsage usage);

		~TextureMetal() override;

	public:
		void Load(std::unique_ptr<std::istream>& stream) override;

		void LoadRaw(void* data, size_t dataSize) override;

		void Bind(ShaderType shader, uint32 slot) override;

		uint32 GetMemorySize() const override;

		void* GetTextureObject() const override { return (__bridge void*)m_texture; }

		void* GetRawTexture() const override { return (__bridge void*)m_texture; }

		void UpdateFromMemory(void* data, size_t dataSize) override;

		void CopyPixelDataTo(uint8* destination) override;

		uint32 GetPixelDataSize() const override;

		/// @brief Copies a whole texture into this one (same size, RGBA8).
		void CopyFrom(id<MTLTexture> source);

		[[nodiscard]] id<MTLTexture> GetMetalTexture() const { return m_texture; }

	protected:
		GraphicsDeviceMetal& m_device;
		BufferUsage m_usage;
		id<MTLTexture> m_texture = nil;
	};

	class RenderTextureMetal final : public RenderTexture
	{
	public:
		RenderTextureMetal(GraphicsDeviceMetal& device, std::string name, uint16 width, uint16 height, RenderTextureFlags flags, PixelFormat colorFormat, PixelFormat depthFormat);

		~RenderTextureMetal() override;

	public:
		// RenderTarget
		void Activate() override;

		void ApplyPendingResize() override;

		void Clear(ClearFlags flags) override;

		void Resize(uint16 width, uint16 height) override;

		void Update() override {}

		// RenderTexture / Texture
		TexturePtr StoreToTexture() override;

		void BindStencil(ShaderType shader, uint32 slot) override;

		void LoadRaw(void* data, size_t dataSize) override {}

		void Bind(ShaderType shader, uint32 slot) override;

		void* GetTextureObject() const override { return (__bridge void*)GetShaderTexture(); }

		void* GetRawTexture() const override { return (__bridge void*)GetShaderTexture(); }

		void UpdateFromMemory(void* data, size_t dataSize) override {}

		void CopyPixelDataTo(uint8* destination) override;

		uint32 GetPixelDataSize() const override;

		[[nodiscard]] id<MTLTexture> GetColorTexture() const { return m_colorTexture; }

		[[nodiscard]] id<MTLTexture> GetDepthTexture() const { return m_depthTexture; }

	private:
		void CreateResources();

		void ReleaseResources();

		[[nodiscard]] id<MTLTexture> GetShaderTexture() const { return m_colorTexture ? m_colorTexture : m_depthTexture; }

	private:
		GraphicsDeviceMetal& m_device;
		id<MTLTexture> m_colorTexture = nil;
		id<MTLTexture> m_depthTexture = nil;
		id<MTLTexture> m_stencilView = nil;
		bool m_resizePending = false;
	};

	class VolumeTextureMetal final : public VolumeTexture
	{
	public:
		VolumeTextureMetal(GraphicsDeviceMetal& device, uint16 width, uint16 height, uint16 depth, VolumeFormat format, bool writable);

	public:
		void Bind(ShaderType stage, uint32 slot) override;

		void BindWritable(uint32 slot) override;

		void Upload(const uint8* data, size_t size) override;

		[[nodiscard]] bool IsValid() const { return m_texture != nil; }

	private:
		GraphicsDeviceMetal& m_device;
		id<MTLTexture> m_texture = nil;
	};

	class TextureArrayMetal final : public TextureArray
	{
	public:
		TextureArrayMetal(GraphicsDeviceMetal& device, uint16 width, uint16 height, uint16 layers, TextureArrayFormat format, bool writable, bool mipmapped);

	public:
		void Bind(ShaderType stage, uint32 slot) override;

		void Unbind(ShaderType stage, uint32 slot) override;

		void BindWritable(uint32 slot) override;

		void Upload(uint32 layer, const uint8* data, size_t size) override;

		void GenerateMips() override;

		[[nodiscard]] bool IsValid() const { return m_texture != nil; }

	private:
		GraphicsDeviceMetal& m_device;
		id<MTLTexture> m_texture = nil;
		/// View of mip 0 for compute writes; Metal can only write one mip level per binding.
		id<MTLTexture> m_writeView = nil;
	};

	class SamplerStateMetal final : public SamplerState
	{
	public:
		SamplerStateMetal(GraphicsDeviceMetal& device, const SamplerDesc& desc);

	public:
		void Bind(ShaderType stage, uint32 slot) override;

	private:
		GraphicsDeviceMetal& m_device;
		id<MTLSamplerState> m_sampler = nil;
	};
}
