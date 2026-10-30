// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "metal_resources.h"

#include "base/macros.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cstring>

namespace mmo
{
	namespace
	{
		MTLResourceOptions SharedBufferOptions()
		{
			return MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined | MTLResourceHazardTrackingModeTracked;
		}

		/// Bytes of one row of blocks (or pixels) of a mip level.
		uint32 RowPitch(const tex::v1_0::PixelFormat format, const uint32 width)
		{
			const uint32 blocksWide = std::max(1u, (width + 3u) / 4u);
			switch (format)
			{
			case tex::v1_0::DXT1:
			case tex::v1_0::BC4:
				return blocksWide * 8u;
			case tex::v1_0::DXT5:
			case tex::v1_0::BC5:
				return blocksWide * 16u;
			case tex::v1_0::R8:
				return width;
			case tex::v1_0::RG8:
				return width * 2u;
			default:
				return width * 4u;
			}
		}

		uint32 RowCount(const tex::v1_0::PixelFormat format, const uint32 height)
		{
			switch (format)
			{
			case tex::v1_0::DXT1:
			case tex::v1_0::DXT5:
			case tex::v1_0::BC4:
			case tex::v1_0::BC5:
				return std::max(1u, (height + 3u) / 4u);
			default:
				return height;
			}
		}

		MTLPixelFormat ToMetalTextureFormat(const tex::v1_0::PixelFormat format)
		{
			switch (format)
			{
			case tex::v1_0::RGB:
			case tex::v1_0::RGBA:
				return MTLPixelFormatRGBA8Unorm;
			case tex::v1_0::DXT1:
				return MTLPixelFormatBC1_RGBA;
			case tex::v1_0::DXT5:
				return MTLPixelFormatBC3_RGBA;
			case tex::v1_0::R8:
				return MTLPixelFormatR8Unorm;
			case tex::v1_0::RG8:
				return MTLPixelFormatRG8Unorm;
			case tex::v1_0::BC4:
				return MTLPixelFormatBC4_RUnorm;
			case tex::v1_0::BC5:
				return MTLPixelFormatBC5_RGUnorm;
			default:
				return MTLPixelFormatInvalid;
			}
		}

		/// Copies data into a texture region through a staging buffer, so the texture can live in
		/// private (GPU optimal) memory.
		void UploadToTexture(GraphicsDeviceMetal& device, id<MTLTexture> texture, const void* data, const NSUInteger bytesPerRow, const NSUInteger bytesPerImage, const MTLRegion region, const NSUInteger level, const NSUInteger slice)
		{
			const NSUInteger totalSize = bytesPerImage;
			id<MTLBuffer> staging = [device.GetDevice() newBufferWithBytes:data length:totalSize options:MTLResourceStorageModeShared];
			id<MTLBlitCommandEncoder> blit = device.GetUploadEncoder();
			[blit copyFromBuffer:staging
				sourceOffset:0
				sourceBytesPerRow:bytesPerRow
				sourceBytesPerImage:bytesPerImage
				sourceSize:region.size
				toTexture:texture
				destinationSlice:slice
				destinationLevel:level
				destinationOrigin:region.origin];
		}
	}

	// ----------------------------------------------------------------------------------------
	// RenamedMetalBuffer

	RenamedMetalBuffer::RenamedMetalBuffer(GraphicsDeviceMetal& device, const size_t size, const void* initialData)
		: m_device(device)
		, m_size(std::max<size_t>(size, 4))
	{
		Instance instance;
		instance.buffer = initialData
			? [device.GetDevice() newBufferWithBytes:initialData length:m_size options:SharedBufferOptions()]
			: [device.GetDevice() newBufferWithLength:m_size options:SharedBufferOptions()];
		m_instances.push_back(instance);
	}

	void* RenamedMetalBuffer::Map(const LockOptions options)
	{
		if (options == LockOptions::ReadOnly || options == LockOptions::NoOverwrite)
		{
			return [m_instances[m_current].buffer contents];
		}

		const uint64 completed = m_device.GetCompletedFrame();
		if (m_instances[m_current].lastUsedFrame > completed)
		{
			// The GPU may still read the current contents: switch to an idle instance.
			size_t idle = m_instances.size();
			for (size_t i = 0; i < m_instances.size(); ++i)
			{
				if (m_instances[i].lastUsedFrame <= completed)
				{
					idle = i;
					break;
				}
			}

			if (idle == m_instances.size())
			{
				Instance instance;
				instance.buffer = [m_device.GetDevice() newBufferWithLength:m_size options:SharedBufferOptions()];
				m_instances.push_back(instance);
			}

			m_current = idle;
		}

		return [m_instances[m_current].buffer contents];
	}

	id<MTLBuffer> RenamedMetalBuffer::Use()
	{
		m_instances[m_current].lastUsedFrame = m_device.GetCurrentFrame();
		return m_instances[m_current].buffer;
	}

	// ----------------------------------------------------------------------------------------
	// ShaderMetal

	ShaderMetal::ShaderMetal(GraphicsDeviceMetal& device, const ShaderType type, id<MTLFunction> function, const uint32 threadgroupSize[3])
		: m_device(device)
		, m_type(type)
		, m_function(function)
		, m_threadgroupSize(MTLSizeMake(threadgroupSize[0], threadgroupSize[1], threadgroupSize[2]))
	{
	}

	ShaderMetal::~ShaderMetal()
	{
		m_device.ForgetShader(this);
	}

	void ShaderMetal::Set()
	{
		m_device.SetShader(this);
	}

	// ----------------------------------------------------------------------------------------
	// Vertex and index buffers

	VertexBufferMetal::VertexBufferMetal(GraphicsDeviceMetal& device, const size_t vertexCount, const size_t vertexSize, const BufferUsage usage, const void* initialData)
		: VertexBuffer(static_cast<uint32>(vertexCount), static_cast<uint32>(vertexSize), usage)
		, m_device(device)
		, m_buffer(device, vertexCount * vertexSize, initialData)
	{
	}

	VertexBufferMetal::~VertexBufferMetal()
	{
		m_device.ForgetVertexBuffer(this);
	}

	void* VertexBufferMetal::Map(const LockOptions lock)
	{
		return m_buffer.Map(lock);
	}

	void VertexBufferMetal::Set(const uint16 slot)
	{
		m_device.SetVertexBuffer(slot, this);
	}

	VertexBufferPtr VertexBufferMetal::Clone()
	{
		return std::make_shared<VertexBufferMetal>(m_device, m_vertexCount, m_vertexSize, m_usage, [m_buffer.Current() contents]);
	}

	IndexBufferMetal::IndexBufferMetal(GraphicsDeviceMetal& device, const size_t indexCount, const IndexBufferSize indexSize, BufferUsage, const void* initialData)
		: IndexBuffer(indexCount, indexSize)
		, m_device(device)
		, m_buffer(device, indexCount * (indexSize == IndexBufferSize::Index_16 ? 2 : 4), initialData)
	{
	}

	IndexBufferMetal::~IndexBufferMetal()
	{
		m_device.ForgetIndexBuffer(this);
	}

	void* IndexBufferMetal::Map(const LockOptions lock)
	{
		return m_buffer.Map(lock);
	}

	void IndexBufferMetal::Set(uint16)
	{
		m_device.SetIndexBuffer(this);
	}

	// ----------------------------------------------------------------------------------------
	// Constant and structured buffers

	ConstantBufferMetal::ConstantBufferMetal(GraphicsDeviceMetal& device, const size_t size, const void* initialData)
		: ConstantBuffer(size)
		, m_device(device)
		, m_data(size, 0)
	{
		if (initialData)
		{
			std::memcpy(m_data.data(), initialData, size);
		}
	}

	ConstantBufferMetal::~ConstantBufferMetal()
	{
		m_device.ForgetConstantBuffer(this);
	}

	void ConstantBufferMetal::BindToStage(const ShaderType type, const uint32 slot)
	{
		m_device.BindConstantBuffer(type, slot, this);
	}

	void ConstantBufferMetal::Update(void* data)
	{
		std::memcpy(m_data.data(), data, m_size);
		++m_version;
	}

	StructuredBufferMetal::StructuredBufferMetal(GraphicsDeviceMetal& device, const size_t elementSize, const size_t elementCount, const void* initialData)
		: StructuredBuffer(elementSize, elementCount)
		, m_device(device)
		, m_buffer(device, elementSize * elementCount, initialData)
	{
	}

	void StructuredBufferMetal::BindToStage(const ShaderType type, const uint32 slot)
	{
		m_device.BindStageBuffer(type, metal_bindings::StructuredBufferBase + slot, m_buffer.Use(), 0);
	}

	void StructuredBufferMetal::Update(const void* data, const size_t elementCount)
	{
		const size_t count = std::min(elementCount, m_elementCount);
		void* destination = m_buffer.Map(LockOptions::Discard);
		std::memcpy(destination, data, count * m_elementSize);
	}

	// ----------------------------------------------------------------------------------------
	// TextureMetal

	TextureMetal::TextureMetal(GraphicsDeviceMetal& device, const uint16 width, const uint16 height, const BufferUsage usage)
		: m_device(device)
		, m_usage(usage)
	{
		m_header.width = width;
		m_header.height = height;
		m_header.format = tex::v1_0::RGBA;
		m_mipCount = 1;
	}

	TextureMetal::~TextureMetal() = default;

	void TextureMetal::Load(std::unique_ptr<std::istream>& stream)
	{
		Texture::Load(stream);

		if (m_header.mipmapOffsets[0] == 0 || m_header.mipmapLengths[0] == 0)
		{
			throw std::runtime_error("Invalid or missing texture data");
		}

		const MTLPixelFormat pixelFormat = ToMetalTextureFormat(m_header.format);
		if (pixelFormat == MTLPixelFormatInvalid)
		{
			throw std::runtime_error("Unsupported texture format for metal texture!");
		}

		// Count the mip levels the file provides, the same way the D3D11 backend does.
		uint32 mipCount = 0;
		for (uint32 level = 0; level < 16; ++level)
		{
			const int32 width = m_header.width >> level;
			const int32 height = m_header.height >> level;
			if (width < 1 || height < 1 || m_header.mipmapLengths[level] == 0)
			{
				break;
			}

			++mipCount;
			if (width <= 1 || height <= 1)
			{
				break;
			}
		}

		MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat width:m_header.width height:m_header.height mipmapped:NO];
		descriptor.mipmapLevelCount = mipCount;
		descriptor.usage = MTLTextureUsageShaderRead;
		descriptor.storageMode = MTLStorageModePrivate;
		m_texture = [m_device.GetDevice() newTextureWithDescriptor:descriptor];
		m_mipCount = mipCount;

		if (!GetDebugName().empty())
		{
			m_texture.label = [NSString stringWithUTF8String:GetDebugName().c_str()];
		}

		std::vector<uint8> mipData;
		for (uint32 level = 0; level < mipCount; ++level)
		{
			mipData.resize(m_header.mipmapLengths[level]);
			stream->seekg(m_header.mipmapOffsets[level], std::ios::beg);
			stream->read(reinterpret_cast<char*>(mipData.data()), m_header.mipmapLengths[level]);

			const uint32 width = std::max(1, m_header.width >> level);
			const uint32 height = std::max(1, m_header.height >> level);
			const uint32 rowPitch = RowPitch(m_header.format, width);
			const uint32 imageSize = rowPitch * RowCount(m_header.format, height);
			if (mipData.size() < imageSize)
			{
				WLOG("Texture " << GetDebugName() << " mip " << level << " has " << mipData.size() << " bytes, expected " << imageSize);
				mipData.resize(imageSize, 0);
			}

			UploadToTexture(m_device, m_texture, mipData.data(), rowPitch, imageSize, MTLRegionMake2D(0, 0, width, height), level, 0);
		}
	}

	void TextureMetal::LoadRaw(void* data, const size_t dataSize)
	{
		MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:std::max<uint16>(1, m_header.width) height:std::max<uint16>(1, m_header.height) mipmapped:NO];
		descriptor.usage = MTLTextureUsageShaderRead;
		descriptor.storageMode = MTLStorageModeShared;
		m_texture = [m_device.GetDevice() newTextureWithDescriptor:descriptor];
		m_mipCount = 1;

		if (data && dataSize >= static_cast<size_t>(m_header.width) * m_header.height * 4)
		{
			[m_texture replaceRegion:MTLRegionMake2D(0, 0, m_header.width, m_header.height) mipmapLevel:0 withBytes:data bytesPerRow:m_header.width * 4];
		}
	}

	void TextureMetal::UpdateFromMemory(void* data, const size_t dataSize)
	{
		ASSERT(data);

		if (!m_texture)
		{
			LoadRaw(nullptr, 0);
		}

		if (dataSize < static_cast<size_t>(m_header.width) * m_header.height * 4)
		{
			return;
		}

		[m_texture replaceRegion:MTLRegionMake2D(0, 0, m_header.width, m_header.height) mipmapLevel:0 withBytes:data bytesPerRow:m_header.width * 4];
	}

	void TextureMetal::Bind(const ShaderType shader, const uint32 slot)
	{
		m_device.BindStageTexture(shader, slot, m_texture);
		m_device.SetTextureAddressMode(GetTextureAddressModeU(), GetTextureAddressModeV(), GetTextureAddressModeW());
		m_device.SetTextureFilter(GetTextureFilter());
	}

	uint32 TextureMetal::GetMemorySize() const
	{
		uint32 size = 0;
		for (uint32 level = 0; level < m_mipCount; ++level)
		{
			const uint32 width = std::max(1, m_header.width >> level);
			const uint32 height = std::max(1, m_header.height >> level);
			size += RowPitch(m_header.format, width) * RowCount(m_header.format, height);
		}

		return size;
	}

	uint32 TextureMetal::GetPixelDataSize() const
	{
		return RowPitch(m_header.format, m_header.width) * RowCount(m_header.format, m_header.height);
	}

	void TextureMetal::CopyPixelDataTo(uint8* destination)
	{
		if (!m_texture)
		{
			return;
		}

		const uint32 rowPitch = RowPitch(m_header.format, m_header.width);
		const uint32 imageSize = GetPixelDataSize();
		id<MTLBuffer> readback = [m_device.GetDevice() newBufferWithLength:imageSize options:MTLResourceStorageModeShared];

		id<MTLBlitCommandEncoder> blit = m_device.BeginFrameBlit();
		[blit copyFromTexture:m_texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(m_header.width, m_header.height, 1)
			toBuffer:readback destinationOffset:0 destinationBytesPerRow:rowPitch destinationBytesPerImage:imageSize];
		m_device.FinishAndWait();

		std::memcpy(destination, [readback contents], imageSize);
	}

	void TextureMetal::CopyFrom(id<MTLTexture> source)
	{
		MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:source.pixelFormat width:source.width height:source.height mipmapped:NO];
		descriptor.usage = MTLTextureUsageShaderRead;
		descriptor.storageMode = MTLStorageModePrivate;
		m_texture = [m_device.GetDevice() newTextureWithDescriptor:descriptor];
		m_header.width = static_cast<uint16>(source.width);
		m_header.height = static_cast<uint16>(source.height);
		m_header.format = tex::v1_0::RGBA;
		m_mipCount = 1;

		id<MTLBlitCommandEncoder> blit = m_device.BeginFrameBlit();
		[blit copyFromTexture:source toTexture:m_texture];
	}

	// ----------------------------------------------------------------------------------------
	// RenderTextureMetal

	RenderTextureMetal::RenderTextureMetal(GraphicsDeviceMetal& device, std::string name, const uint16 width, const uint16 height, const RenderTextureFlags flags, const PixelFormat colorFormat, const PixelFormat depthFormat)
		: RenderTexture(std::move(name), width, height, flags, colorFormat, depthFormat)
		, m_device(device)
	{
		ASSERT(width > 0);
		ASSERT(height > 0);
		CreateResources();
	}

	RenderTextureMetal::~RenderTextureMetal()
	{
		ReleaseResources();
	}

	void RenderTextureMetal::ReleaseResources()
	{
		if (m_colorTexture)
		{
			m_device.ForgetTexture(m_colorTexture);
		}
		if (m_depthTexture)
		{
			m_device.ForgetTexture(m_depthTexture);
		}

		m_colorTexture = nil;
		m_depthTexture = nil;
		m_stencilView = nil;
	}

	void RenderTextureMetal::CreateResources()
	{
		m_header.width = m_width;
		m_header.height = m_height;

		const NSString* label = [NSString stringWithUTF8String:m_name.c_str()];

		if (HasColorBuffer())
		{
			MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:ToMetalRenderTargetFormat(m_colorFormat) width:m_width height:m_height mipmapped:NO];
			descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
			descriptor.storageMode = MTLStorageModePrivate;
			m_colorTexture = [m_device.GetDevice() newTextureWithDescriptor:descriptor];
			m_colorTexture.label = (NSString*)label;
		}

		if (HasDepthBuffer())
		{
			// D3D11 renders into a D24S8 buffer. Apple GPUs have no 24 bit depth, so this is the
			// closest format that still carries the stencil plane the G-Buffer pass tags pixels in.
			MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:m_width height:m_height mipmapped:NO];
			descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
			descriptor.storageMode = MTLStorageModePrivate;
			m_depthTexture = [m_device.GetDevice() newTextureWithDescriptor:descriptor];
			m_depthTexture.label = [NSString stringWithFormat:@"%@ (depth)", label];

			// D3D11 shaders read the stencil from the green channel of an X24_G8 view; repeat the
			// stencil value into every channel so that keeps working whatever channel Metal uses.
			const MTLTextureSwizzleChannels swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed, MTLTextureSwizzleRed, MTLTextureSwizzleRed, MTLTextureSwizzleRed);
			m_stencilView = [m_depthTexture newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8 textureType:MTLTextureType2D levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1) swizzle:swizzle];
		}
	}

	void RenderTextureMetal::Activate()
	{
		ApplyPendingResize();
		RenderTarget::Activate();

		const id<MTLTexture> colors[1] = { m_colorTexture };
		m_device.SetRenderTargetTextures(colors, m_colorTexture ? 1 : 0, m_depthTexture);
		m_device.SetViewport(0, 0, m_width, m_height, 0.0f, 1.0f);
	}

	void RenderTextureMetal::ApplyPendingResize()
	{
		if (m_resizePending)
		{
			m_resizePending = false;
			ReleaseResources();
			CreateResources();
		}
	}

	void RenderTextureMetal::Clear(const ClearFlags flags)
	{
		m_device.ClearRenderTexture(m_colorTexture, m_depthTexture, flags);
	}

	void RenderTextureMetal::Resize(const uint16 width, const uint16 height)
	{
		if (width == m_width && height == m_height)
		{
			return;
		}

		m_width = width;
		m_height = height;
		m_resizePending = true;
	}

	TexturePtr RenderTextureMetal::StoreToTexture()
	{
		auto texture = std::make_shared<TextureMetal>(m_device, m_width, m_height, BufferUsage::Static);
		if (m_colorTexture)
		{
			texture->CopyFrom(m_colorTexture);
		}

		return texture;
	}

	void RenderTextureMetal::Bind(const ShaderType shader, const uint32 slot)
	{
		m_device.BindStageTexture(shader, slot, GetShaderTexture());
	}

	void RenderTextureMetal::BindStencil(const ShaderType shader, const uint32 slot)
	{
		m_device.BindStageTexture(shader, slot, m_stencilView);
	}

	uint32 RenderTextureMetal::GetPixelDataSize() const
	{
		uint32 bytesPerPixel = 4;
		switch (m_colorFormat)
		{
		case PixelFormat::R16G16B16A16:
			bytesPerPixel = 8;
			break;
		case PixelFormat::R32G32B32A32:
			bytesPerPixel = 16;
			break;
		case PixelFormat::R8:
			bytesPerPixel = 1;
			break;
		default:
			break;
		}

		return m_width * m_height * bytesPerPixel;
	}

	void RenderTextureMetal::CopyPixelDataTo(uint8* destination)
	{
		if (!m_colorTexture)
		{
			return;
		}

		const uint32 imageSize = GetPixelDataSize();
		const uint32 rowPitch = imageSize / m_height;
		id<MTLBuffer> readback = [m_device.GetDevice() newBufferWithLength:imageSize options:MTLResourceStorageModeShared];

		id<MTLBlitCommandEncoder> blit = m_device.BeginFrameBlit();
		[blit copyFromTexture:m_colorTexture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(m_width, m_height, 1)
			toBuffer:readback destinationOffset:0 destinationBytesPerRow:rowPitch destinationBytesPerImage:imageSize];
		m_device.FinishAndWait();

		std::memcpy(destination, [readback contents], imageSize);
	}

	// ----------------------------------------------------------------------------------------
	// VolumeTextureMetal

	VolumeTextureMetal::VolumeTextureMetal(GraphicsDeviceMetal& device, const uint16 width, const uint16 height, const uint16 depth, const VolumeFormat format, const bool writable)
		: VolumeTexture(width, height, depth, format, writable)
		, m_device(device)
	{
		MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
		descriptor.textureType = MTLTextureType3D;
		descriptor.pixelFormat = format == VolumeFormat::R8 ? MTLPixelFormatR8Unorm : MTLPixelFormatRGBA16Float;
		descriptor.width = width;
		descriptor.height = height;
		descriptor.depth = depth;
		descriptor.usage = MTLTextureUsageShaderRead | (writable ? MTLTextureUsageShaderWrite : 0);
		descriptor.storageMode = MTLStorageModePrivate;
		m_texture = [device.GetDevice() newTextureWithDescriptor:descriptor];
	}

	void VolumeTextureMetal::Bind(const ShaderType stage, const uint32 slot)
	{
		m_device.BindStageTexture(stage, slot, m_texture);
	}

	void VolumeTextureMetal::BindWritable(const uint32 slot)
	{
		m_device.BindStageTexture(ShaderType::ComputeShader, metal_bindings::RWTextureBase + slot, m_texture);
	}

	void VolumeTextureMetal::Upload(const uint8* data, const size_t size)
	{
		const NSUInteger bytesPerTexel = m_format == VolumeFormat::R8 ? 1 : 8;
		const NSUInteger rowPitch = m_width * bytesPerTexel;
		const NSUInteger slicePitch = rowPitch * m_height;
		if (size < slicePitch * m_depth)
		{
			return;
		}

		id<MTLBuffer> staging = [m_device.GetDevice() newBufferWithBytes:data length:slicePitch * m_depth options:MTLResourceStorageModeShared];
		id<MTLBlitCommandEncoder> blit = m_device.GetUploadEncoder();
		[blit copyFromBuffer:staging sourceOffset:0 sourceBytesPerRow:rowPitch sourceBytesPerImage:slicePitch sourceSize:MTLSizeMake(m_width, m_height, m_depth)
			toTexture:m_texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
	}

	// ----------------------------------------------------------------------------------------
	// TextureArrayMetal

	namespace
	{
		MTLPixelFormat ToMetalArrayFormat(const TextureArrayFormat format)
		{
			switch (format)
			{
			case TextureArrayFormat::RG8:
				return MTLPixelFormatRG8Unorm;
			case TextureArrayFormat::RGBA8:
				return MTLPixelFormatRGBA8Unorm;
			case TextureArrayFormat::R32F:
				return MTLPixelFormatR32Float;
			case TextureArrayFormat::RGBA16F:
				return MTLPixelFormatRGBA16Float;
			case TextureArrayFormat::RGBA32F:
				return MTLPixelFormatRGBA32Float;
			}

			return MTLPixelFormatRGBA8Unorm;
		}

		NSUInteger BytesPerTexel(const TextureArrayFormat format)
		{
			switch (format)
			{
			case TextureArrayFormat::RG8:
				return 2;
			case TextureArrayFormat::RGBA8:
			case TextureArrayFormat::R32F:
				return 4;
			case TextureArrayFormat::RGBA16F:
				return 8;
			case TextureArrayFormat::RGBA32F:
				return 16;
			}

			return 4;
		}
	}

	TextureArrayMetal::TextureArrayMetal(GraphicsDeviceMetal& device, const uint16 width, const uint16 height, const uint16 layers, const TextureArrayFormat format, const bool writable, const bool mipmapped)
		: TextureArray(width, height, layers, format, writable, mipmapped)
		, m_device(device)
	{
		MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
		descriptor.textureType = MTLTextureType2DArray;
		descriptor.pixelFormat = ToMetalArrayFormat(format);
		descriptor.width = width;
		descriptor.height = height;
		descriptor.arrayLength = layers;
		descriptor.mipmapLevelCount = mipmapped ? static_cast<NSUInteger>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;
		descriptor.usage = MTLTextureUsageShaderRead | (writable ? MTLTextureUsageShaderWrite : 0) | MTLTextureUsagePixelFormatView;
		descriptor.storageMode = MTLStorageModePrivate;
		m_texture = [device.GetDevice() newTextureWithDescriptor:descriptor];

		if (m_texture && writable)
		{
			m_writeView = descriptor.mipmapLevelCount > 1
				? [m_texture newTextureViewWithPixelFormat:descriptor.pixelFormat textureType:MTLTextureType2DArray levels:NSMakeRange(0, 1) slices:NSMakeRange(0, layers)]
				: m_texture;
		}
	}

	void TextureArrayMetal::Bind(const ShaderType stage, const uint32 slot)
	{
		m_device.BindStageTexture(stage, slot, m_texture);
	}

	void TextureArrayMetal::Unbind(const ShaderType stage, const uint32 slot)
	{
		m_device.BindStageTexture(stage, slot, nil);
	}

	void TextureArrayMetal::BindWritable(const uint32 slot)
	{
		m_device.BindStageTexture(ShaderType::ComputeShader, metal_bindings::RWTextureBase + slot, m_writeView);
	}

	void TextureArrayMetal::Upload(const uint32 layer, const uint8* data, const size_t size)
	{
		const NSUInteger rowPitch = m_width * BytesPerTexel(m_format);
		const NSUInteger imageSize = rowPitch * m_height;
		if (size < imageSize || layer >= m_layers)
		{
			return;
		}

		UploadToTexture(m_device, m_texture, data, rowPitch, imageSize, MTLRegionMake2D(0, 0, m_width, m_height), 0, layer);
	}

	void TextureArrayMetal::GenerateMips()
	{
		if (!m_mipmapped || m_texture.mipmapLevelCount <= 1)
		{
			return;
		}

		id<MTLBlitCommandEncoder> blit = m_device.BeginFrameBlit();
		[blit generateMipmapsForTexture:m_texture];
	}

	// ----------------------------------------------------------------------------------------
	// SamplerStateMetal

	SamplerStateMetal::SamplerStateMetal(GraphicsDeviceMetal& device, const SamplerDesc& desc)
		: m_device(device)
	{
		MTLSamplerDescriptor* descriptor = [[MTLSamplerDescriptor alloc] init];

		switch (desc.filter)
		{
		case SamplerFilter::Point:
			descriptor.minFilter = MTLSamplerMinMagFilterNearest;
			descriptor.magFilter = MTLSamplerMinMagFilterNearest;
			descriptor.mipFilter = MTLSamplerMipFilterNearest;
			break;
		case SamplerFilter::Anisotropic:
		case SamplerFilter::ComparisonAnisotropic:
			descriptor.minFilter = MTLSamplerMinMagFilterLinear;
			descriptor.magFilter = MTLSamplerMinMagFilterLinear;
			descriptor.mipFilter = MTLSamplerMipFilterLinear;
			descriptor.maxAnisotropy = std::clamp<uint32>(desc.maxAnisotropy, 1, 16);
			break;
		default:
			descriptor.minFilter = MTLSamplerMinMagFilterLinear;
			descriptor.magFilter = MTLSamplerMinMagFilterLinear;
			descriptor.mipFilter = MTLSamplerMipFilterLinear;
			break;
		}

		MTLSamplerAddressMode address = MTLSamplerAddressModeClampToEdge;
		switch (desc.address)
		{
		case SamplerAddress::Wrap:
			address = MTLSamplerAddressModeRepeat;
			break;
		case SamplerAddress::Border:
			address = MTLSamplerAddressModeClampToBorderColor;
			break;
		default:
			break;
		}

		descriptor.sAddressMode = address;
		descriptor.tAddressMode = address;
		descriptor.rAddressMode = address;

		// Metal only offers three border colors; pick the closest one.
		if (desc.borderColor[3] < 0.5f)
		{
			descriptor.borderColor = MTLSamplerBorderColorTransparentBlack;
		}
		else if (desc.borderColor[0] > 0.5f)
		{
			descriptor.borderColor = MTLSamplerBorderColorOpaqueWhite;
		}
		else
		{
			descriptor.borderColor = MTLSamplerBorderColorOpaqueBlack;
		}

		if (desc.filter == SamplerFilter::ComparisonLinear || desc.filter == SamplerFilter::ComparisonAnisotropic)
		{
			switch (desc.comparison)
			{
			case SamplerComparison::Less:
				descriptor.compareFunction = MTLCompareFunctionLess;
				break;
			case SamplerComparison::Always:
				descriptor.compareFunction = MTLCompareFunctionAlways;
				break;
			default:
				descriptor.compareFunction = MTLCompareFunctionLessEqual;
				break;
			}
		}

		m_sampler = device.GetSampler(descriptor);
	}

	void SamplerStateMetal::Bind(const ShaderType stage, const uint32 slot)
	{
		m_device.BindStageSampler(stage, slot, m_sampler);
	}
}
