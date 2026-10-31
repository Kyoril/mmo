// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graphics_device_metal.h"
#include "graphics_device_metal_factory.h"

#include "metal_resources.h"
#include "render_window_metal.h"

#include "base/macros.h"
#include "base/profiler.h"
#include "graphics/global_shader_parameters.h"
#include "graphics/material.h"
#include "graphics_null/material_compiler_null.h"
#include "graphics_null/shader_compiler_null.h"
#include "log/default_log_levels.h"
#include "math/radian.h"
#include "scene_graph/render_operation.h"

#include "VS_Pos.metal.h"
#include "VS_PosColor.metal.h"
#include "VS_PosColorNormal.metal.h"
#include "VS_PosColorNormalTex.metal.h"
#include "VS_PosColorNormalBinormalTangentTex.metal.h"
#include "VS_PosColorTex.metal.h"
#include "PS_Pos.metal.h"
#include "PS_PosColor.metal.h"
#include "PS_PosColorNormal.metal.h"
#include "PS_PosColorNormalTex.metal.h"
#include "PS_PosColorNormalBinormalTangentTex.metal.h"
#include "PS_PosColorTex.metal.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>
#include <string_view>

namespace mmo
{
	std::unique_ptr<GraphicsDevice> CreateGraphicsDeviceMetal()
	{
		return std::make_unique<GraphicsDeviceMetal>();
	}

	namespace
	{
		constexpr uint32 NullVertexBufferIndex = metal_bindings::VertexBufferTop - metal_bindings::MaxVertexStreams;
		constexpr NSUInteger TransientChunkSize = 4 * 1024 * 1024;
		constexpr NSUInteger ZeroBufferSize = 64 * 1024;
		constexpr size_t MaxSetBytesSize = 4096;

		uint64 HashBytes(const void* data, const size_t size, uint64 hash = 1469598103934665603ull)
		{
			const auto* bytes = static_cast<const uint8*>(data);
			for (size_t i = 0; i < size; ++i)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}

			return hash;
		}

		template<typename T>
		uint64 HashValue(const T& value, const uint64 hash)
		{
			return HashBytes(&value, sizeof(T), hash);
		}

		MTLVertexFormat ToMetalVertexFormat(const VertexElementType type)
		{
			switch (type)
			{
			case VertexElementType::Float1: return MTLVertexFormatFloat;
			case VertexElementType::Float2: return MTLVertexFormatFloat2;
			case VertexElementType::Float3: return MTLVertexFormatFloat3;
			case VertexElementType::Float4: return MTLVertexFormatFloat4;
			// Same as D3D11, which reads every color type as R8G8B8A8_UNORM.
			case VertexElementType::Color:
			case VertexElementType::ColorAbgr:
			case VertexElementType::ColorArgb: return MTLVertexFormatUChar4Normalized;
			case VertexElementType::UByte4: return MTLVertexFormatUChar4;
			case VertexElementType::Int1: return MTLVertexFormatInt;
			case VertexElementType::Int2: return MTLVertexFormatInt2;
			case VertexElementType::Int3: return MTLVertexFormatInt3;
			case VertexElementType::Int4: return MTLVertexFormatInt4;
			case VertexElementType::UInt1: return MTLVertexFormatUInt;
			case VertexElementType::UInt2: return MTLVertexFormatUInt2;
			case VertexElementType::UInt3: return MTLVertexFormatUInt3;
			case VertexElementType::UInt4: return MTLVertexFormatUInt4;
			case VertexElementType::Short1: return MTLVertexFormatShort;
			case VertexElementType::Short2: return MTLVertexFormatShort2;
			case VertexElementType::Short3: return MTLVertexFormatShort3;
			case VertexElementType::Short4: return MTLVertexFormatShort4;
			case VertexElementType::UShort1: return MTLVertexFormatUShort;
			case VertexElementType::UShort2: return MTLVertexFormatUShort2;
			case VertexElementType::UShort3: return MTLVertexFormatUShort3;
			case VertexElementType::UShort4: return MTLVertexFormatUShort4;
			default: return MTLVertexFormatInvalid;
			}
		}

		/// Format to feed a shader input nothing in the vertex layout provides (zeros).
		MTLVertexFormat PlaceholderVertexFormat(const MTLDataType type)
		{
			switch (type)
			{
			case MTLDataTypeFloat: return MTLVertexFormatFloat;
			case MTLDataTypeFloat2: return MTLVertexFormatFloat2;
			case MTLDataTypeFloat3: return MTLVertexFormatFloat3;
			case MTLDataTypeInt: return MTLVertexFormatInt;
			case MTLDataTypeInt2: return MTLVertexFormatInt2;
			case MTLDataTypeInt3: return MTLVertexFormatInt3;
			case MTLDataTypeInt4: return MTLVertexFormatInt4;
			case MTLDataTypeUInt: return MTLVertexFormatUInt;
			case MTLDataTypeUInt2: return MTLVertexFormatUInt2;
			case MTLDataTypeUInt3: return MTLVertexFormatUInt3;
			case MTLDataTypeUInt4: return MTLVertexFormatUInt4;
			default: return MTLVertexFormatFloat4;
			}
		}

		const char* SemanticName(const VertexElementSemantic semantic)
		{
			switch (semantic)
			{
			case VertexElementSemantic::Position: return "POSITION";
			case VertexElementSemantic::BlendWeights: return "BLENDWEIGHT";
			case VertexElementSemantic::BlendIndices: return "BLENDINDICES";
			case VertexElementSemantic::Normal: return "NORMAL";
			case VertexElementSemantic::Diffuse: return "COLOR";
			case VertexElementSemantic::TextureCoordinate: return "TEXCOORD";
			case VertexElementSemantic::Binormal: return "BINORMAL";
			case VertexElementSemantic::Tangent: return "TANGENT";
			}

			return "";
		}

		/// Splits a vertex attribute name generated by the cross compiler ("TEXCOORD8", or
		/// "TEXCOORD8_2" for the third column of a matrix input) into semantic name and index.
		bool ParseAttributeName(const std::string_view name, std::string& semantic, uint32& index)
		{
			size_t pos = 0;
			while (pos < name.size() && std::isalpha(static_cast<unsigned char>(name[pos])))
			{
				++pos;
			}
			if (pos == 0)
			{
				return false;
			}

			semantic = std::string(name.substr(0, pos));
			index = 0;
			const size_t digitsStart = pos;
			while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos])))
			{
				index = index * 10 + (name[pos] - '0');
				++pos;
			}
			if (pos == digitsStart)
			{
				return false;
			}

			if (pos < name.size() && name[pos] == '_')
			{
				++pos;
				uint32 column = 0;
				while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos])))
				{
					column = column * 10 + (name[pos] - '0');
					++pos;
				}
				index += column;
			}

			return true;
		}

		MTLPrimitiveType ToMetalPrimitive(const TopologyType type)
		{
			switch (type)
			{
			case TopologyType::PointList: return MTLPrimitiveTypePoint;
			case TopologyType::LineList: return MTLPrimitiveTypeLine;
			case TopologyType::LineStrip: return MTLPrimitiveTypeLineStrip;
			case TopologyType::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
			default: return MTLPrimitiveTypeTriangle;
			}
		}

		MTLCompareFunction ToMetalCompare(const DepthTestMethod method)
		{
			switch (method)
			{
			case DepthTestMethod::Never: return MTLCompareFunctionNever;
			case DepthTestMethod::Less: return MTLCompareFunctionLess;
			case DepthTestMethod::Equal: return MTLCompareFunctionEqual;
			case DepthTestMethod::LessEqual: return MTLCompareFunctionLessEqual;
			case DepthTestMethod::Greater: return MTLCompareFunctionGreater;
			case DepthTestMethod::NotEqual: return MTLCompareFunctionNotEqual;
			case DepthTestMethod::GreaterEqual: return MTLCompareFunctionGreaterEqual;
			default: return MTLCompareFunctionAlways;
			}
		}

		MTLSamplerAddressMode ToMetalAddress(const TextureAddressMode mode)
		{
			switch (mode)
			{
			case TextureAddressMode::Clamp: return MTLSamplerAddressModeClampToEdge;
			case TextureAddressMode::Mirror: return MTLSamplerAddressModeMirrorRepeat;
			case TextureAddressMode::Border: return MTLSamplerAddressModeClampToBorderColor;
			default: return MTLSamplerAddressModeRepeat;
			}
		}

		bool HasStencil(const MTLPixelFormat format)
		{
			return format == MTLPixelFormatDepth32Float_Stencil8 || format == MTLPixelFormatStencil8 || format == MTLPixelFormatDepth24Unorm_Stencil8;
		}

		uint32 StageIndex(const ShaderType type)
		{
			switch (type)
			{
			case ShaderType::VertexShader: return 0;
			case ShaderType::ComputeShader: return 2;
			default: return 1;
			}
		}
	}

	MTLPixelFormat ToMetalRenderTargetFormat(const PixelFormat format)
	{
		switch (format)
		{
		case PixelFormat::B8G8R8A8: return MTLPixelFormatBGRA8Unorm;
		case PixelFormat::R16G16B16A16: return MTLPixelFormatRGBA16Float;
		case PixelFormat::R32G32B32A32: return MTLPixelFormatRGBA32Float;
		case PixelFormat::R8: return MTLPixelFormatR8Unorm;
		case PixelFormat::RG8: return MTLPixelFormatRG8Unorm;
		case PixelFormat::R32F: return MTLPixelFormatR32Float;
		case PixelFormat::D32F: return MTLPixelFormatR32Float;
		default: return MTLPixelFormatRGBA8Unorm;
		}
	}

	// ----------------------------------------------------------------------------------------
	// Construction

	GraphicsDeviceMetal::GraphicsDeviceMetal() = default;

	GraphicsDeviceMetal::~GraphicsDeviceMetal()
	{
		if (m_commandQueue)
		{
			EndEncoders();
			CommitUploads();
			if (m_commandBuffer)
			{
				[m_commandBuffer commit];
				[m_commandBuffer waitUntilCompleted];
				m_commandBuffer = nil;
			}
		}

		m_autoCreatedWindow.reset();
		m_formatVertexShaders.clear();
		m_formatPixelShaders.clear();
		m_objectMatrixBuffer.reset();
		m_viewMatrixBuffer.reset();
	}

	void GraphicsDeviceMetal::Create(const GraphicsDeviceDesc& desc)
	{
		GraphicsDevice::Create(desc);

		m_vsync = desc.vsync;

		m_device = MTLCreateSystemDefaultDevice();
		if (!m_device)
		{
			throw std::runtime_error("No Metal capable GPU found");
		}

		ILOG("Metal device: " << [[m_device name] UTF8String]);

		m_commandQueue = [m_device newCommandQueue];
		m_frameSemaphore = dispatch_semaphore_create(MaxFramesInFlight);

		m_zeroBuffer = [m_device newBufferWithLength:ZeroBufferSize options:MTLResourceStorageModeShared];
		std::memset([m_zeroBuffer contents], 0, ZeroBufferSize);
		m_nullVertexBuffer = m_zeroBuffer;

		MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
		samplerDescriptor.minFilter = MTLSamplerMinMagFilterLinear;
		samplerDescriptor.magFilter = MTLSamplerMinMagFilterLinear;
		samplerDescriptor.mipFilter = MTLSamplerMipFilterLinear;
		m_defaultSampler = GetSampler(samplerDescriptor);

		for (auto& stage : m_stages)
		{
			stage.Clear();
		}

		CreateMatrixBuffers();
		CreateBuiltInShaders();
		ApplyEffectiveTextureFilter();

		if (desc.customWindowHandle == nullptr)
		{
			auto window = std::make_shared<RenderWindowMetal>(*this, "__auto_window__", desc.width, desc.height, !desc.windowed);
			m_mainWindow = window.get();
			m_autoCreatedWindow = window;
		}
	}

	void GraphicsDeviceMetal::CreateMatrixBuffers()
	{
		m_transform[World] = Matrix4::Identity;
		m_transform[View] = Matrix4::Identity;
		m_transform[Projection] = Matrix4::Identity;
		m_inverseView = Matrix4::Identity;
		m_inverseProj = Matrix4::Identity;

		m_objectMatrixBuffer = CreateConstantBuffer(sizeof(Matrix4), &m_transform[World]);

		Matrix4 viewMatrices[4] = { m_transform[View], m_transform[Projection], m_inverseView, m_inverseProj };
		m_viewMatrixBuffer = CreateConstantBuffer(sizeof(viewMatrices), viewMatrices);
	}

	void GraphicsDeviceMetal::CreateBuiltInShaders()
	{
		m_formatVertexShaders[VertexFormat::Pos] = CreateShader(ShaderType::VertexShader, g_VS_Pos, sizeof(g_VS_Pos));
		m_formatVertexShaders[VertexFormat::PosColor] = CreateShader(ShaderType::VertexShader, g_VS_PosColor, sizeof(g_VS_PosColor));
		m_formatVertexShaders[VertexFormat::PosColorNormal] = CreateShader(ShaderType::VertexShader, g_VS_PosColorNormal, sizeof(g_VS_PosColorNormal));
		m_formatVertexShaders[VertexFormat::PosColorNormalTex1] = CreateShader(ShaderType::VertexShader, g_VS_PosColorNormalTex, sizeof(g_VS_PosColorNormalTex));
		m_formatVertexShaders[VertexFormat::PosColorNormalBinormalTangentTex1] = CreateShader(ShaderType::VertexShader, g_VS_PosColorNormalBinormalTangentTex, sizeof(g_VS_PosColorNormalBinormalTangentTex));
		m_formatVertexShaders[VertexFormat::PosColorTex1] = CreateShader(ShaderType::VertexShader, g_VS_PosColorTex, sizeof(g_VS_PosColorTex));

		m_formatPixelShaders[VertexFormat::Pos] = CreateShader(ShaderType::PixelShader, g_PS_Pos, sizeof(g_PS_Pos));
		m_formatPixelShaders[VertexFormat::PosColor] = CreateShader(ShaderType::PixelShader, g_PS_PosColor, sizeof(g_PS_PosColor));
		m_formatPixelShaders[VertexFormat::PosColorNormal] = CreateShader(ShaderType::PixelShader, g_PS_PosColorNormal, sizeof(g_PS_PosColorNormal));
		m_formatPixelShaders[VertexFormat::PosColorNormalTex1] = CreateShader(ShaderType::PixelShader, g_PS_PosColorNormalTex, sizeof(g_PS_PosColorNormalTex));
		m_formatPixelShaders[VertexFormat::PosColorNormalBinormalTangentTex1] = CreateShader(ShaderType::PixelShader, g_PS_PosColorNormalBinormalTangentTex, sizeof(g_PS_PosColorNormalBinormalTangentTex));
		m_formatPixelShaders[VertexFormat::PosColorTex1] = CreateShader(ShaderType::PixelShader, g_PS_PosColorTex, sizeof(g_PS_PosColorTex));
	}

	// ----------------------------------------------------------------------------------------
	// Command buffers

	id<MTLCommandBuffer> GraphicsDeviceMetal::GetCommandBuffer()
	{
		if (!m_commandBuffer)
		{
			// Limit how far the CPU can run ahead of the GPU.
			dispatch_semaphore_wait(m_frameSemaphore, DISPATCH_TIME_FOREVER);
			m_commandBuffer = [m_commandQueue commandBuffer];
			m_commandBuffer.label = [NSString stringWithFormat:@"Frame %llu", m_frameIndex];

			const uint64 frame = m_frameIndex;
			std::atomic<uint64>* completed = &m_completedFrame;
			dispatch_semaphore_t semaphore = m_frameSemaphore;
			[m_commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer)
			{
				if (buffer.status == MTLCommandBufferStatusError)
				{
					NSLog(@"Metal command buffer failed: %@", buffer.error);
				}

				uint64 previous = completed->load(std::memory_order_relaxed);
				while (previous < frame && !completed->compare_exchange_weak(previous, frame, std::memory_order_release))
				{
				}
				dispatch_semaphore_signal(semaphore);
			}];
		}

		return m_commandBuffer;
	}

	id<MTLBlitCommandEncoder> GraphicsDeviceMetal::GetUploadEncoder()
	{
		if (!m_uploadEncoder)
		{
			m_uploadCommandBuffer = [m_commandQueue commandBuffer];
			m_uploadCommandBuffer.label = @"Uploads";
			m_uploadEncoder = [m_uploadCommandBuffer blitCommandEncoder];
		}

		return m_uploadEncoder;
	}

	void GraphicsDeviceMetal::CommitUploads()
	{
		if (m_uploadEncoder)
		{
			[m_uploadEncoder endEncoding];
			[m_uploadCommandBuffer commit];
			m_uploadEncoder = nil;
			m_uploadCommandBuffer = nil;
		}
	}

	void GraphicsDeviceMetal::FlushUploadsAndWait()
	{
		if (m_uploadEncoder)
		{
			id<MTLCommandBuffer> uploads = m_uploadCommandBuffer;
			CommitUploads();
			[uploads waitUntilCompleted];
		}
	}

	id<MTLBlitCommandEncoder> GraphicsDeviceMetal::BeginFrameBlit()
	{
		EndEncoders();
		m_blitEncoder = [GetCommandBuffer() blitCommandEncoder];
		return m_blitEncoder;
	}

	void GraphicsDeviceMetal::FinishAndWait()
	{
		EndEncoders();
		CommitUploads();

		id<MTLCommandBuffer> buffer = GetCommandBuffer();
		[buffer commit];
		m_commandBuffer = nil;
		++m_frameIndex;
		[buffer waitUntilCompleted];
	}

	void GraphicsDeviceMetal::FlushCommands()
	{
		EndEncoders();
		CommitUploads();

		if (m_commandBuffer)
		{
			[m_commandBuffer commit];
			m_commandBuffer = nil;
			++m_frameIndex;
		}
	}

	void GraphicsDeviceMetal::PresentFrame(id<CAMetalDrawable> drawable)
	{
		PROFILE_SCOPE("Metal::Present");

		EndEncoders();
		CommitUploads();

		id<MTLCommandBuffer> buffer = GetCommandBuffer();
		if (drawable)
		{
			[buffer presentDrawable:drawable];
		}

		[buffer commit];
		m_commandBuffer = nil;
		++m_frameIndex;

		LatchFrameBatchCount();
	}

	void GraphicsDeviceMetal::LatchFrameBatchCount()
	{
		m_lastFrameBatchCount = m_batchCount;
		m_batchCount = 0;
	}

	// ----------------------------------------------------------------------------------------
	// Encoders and render targets

	void GraphicsDeviceMetal::EndRenderEncoder()
	{
		if (m_renderEncoder)
		{
			[m_renderEncoder endEncoding];
			m_renderEncoder = nil;
		}
	}

	void GraphicsDeviceMetal::EndComputeEncoder()
	{
		if (m_computeEncoder)
		{
			[m_computeEncoder endEncoding];
			m_computeEncoder = nil;
		}
	}

	void GraphicsDeviceMetal::EndEncoders()
	{
		if (m_blitEncoder)
		{
			[m_blitEncoder endEncoding];
			m_blitEncoder = nil;
		}

		EndComputeEncoder();

		// Clears are executed by the next render pass on the targets. If nothing was drawn since,
		// open a pass now so the clear is not lost.
		const bool clearPending = m_pendingClear.depth || m_pendingClear.stencil || std::any_of(std::begin(m_pendingClear.color), std::end(m_pendingClear.color), [](const bool b) { return b; });
		if (!m_renderEncoder && clearPending)
		{
			EnsureRenderEncoder();
		}

		EndRenderEncoder();
	}

	bool GraphicsDeviceMetal::EnsureRenderEncoder()
	{
		if (m_renderEncoder)
		{
			return true;
		}

		if (m_colorTargetCount == 0 && !m_depthTarget)
		{
			return false;
		}

		if (m_blitEncoder)
		{
			[m_blitEncoder endEncoding];
			m_blitEncoder = nil;
		}
		EndComputeEncoder();

		MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
		for (uint32 i = 0; i < m_colorTargetCount; ++i)
		{
			if (!m_colorTargets[i])
			{
				continue;
			}

			MTLRenderPassColorAttachmentDescriptor* attachment = pass.colorAttachments[i];
			attachment.texture = m_colorTargets[i];
			attachment.loadAction = m_pendingClear.color[i] ? MTLLoadActionClear : MTLLoadActionLoad;
			attachment.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
			attachment.storeAction = MTLStoreActionStore;
		}

		if (m_depthTarget)
		{
			pass.depthAttachment.texture = m_depthTarget;
			pass.depthAttachment.loadAction = m_pendingClear.depth ? MTLLoadActionClear : MTLLoadActionLoad;
			pass.depthAttachment.clearDepth = 1.0;
			pass.depthAttachment.storeAction = MTLStoreActionStore;

			if (HasStencil(m_depthTarget.pixelFormat))
			{
				pass.stencilAttachment.texture = m_depthTarget;
				pass.stencilAttachment.loadAction = m_pendingClear.stencil ? MTLLoadActionClear : MTLLoadActionLoad;
				pass.stencilAttachment.clearStencil = 0;
				pass.stencilAttachment.storeAction = MTLStoreActionStore;
			}
		}

		m_pendingClear = PendingClear{};

		m_renderEncoder = [GetCommandBuffer() renderCommandEncoderWithDescriptor:pass];
		m_currentPipeline = nil;
		m_currentPipelineEntry = nullptr;
		m_currentDepthStencil = nil;
		m_rasterStateDirty = true;
		m_viewportDirty = true;
		m_vertexStreamsDirty = true;
		m_stages[0].MarkAllDirty();
		m_stages[1].MarkAllDirty();

		return true;
	}

	void GraphicsDeviceMetal::SetRenderTargetTextures(const id<MTLTexture>* colors, const uint32 colorCount, id<MTLTexture> depth)
	{
		ASSERT(colorCount <= MaxColorTargets);

		bool changed = colorCount != m_colorTargetCount || depth != m_depthTarget;
		for (uint32 i = 0; i < colorCount && !changed; ++i)
		{
			changed = colors[i] != m_colorTargets[i];
		}

		if (!changed)
		{
			return;
		}

		EndEncoders();

		for (uint32 i = 0; i < MaxColorTargets; ++i)
		{
			m_colorTargets[i] = i < colorCount ? colors[i] : nil;
		}
		m_colorTargetCount = colorCount;
		m_depthTarget = depth;
	}

	void GraphicsDeviceMetal::SetRenderTargets(RenderTexturePtr* renderTargets, const uint32 count)
	{
		id<MTLTexture> colors[MaxColorTargets] = {};
		for (uint32 i = 0; i < count && i < MaxColorTargets; ++i)
		{
			colors[i] = static_cast<RenderTextureMetal*>(renderTargets[i].get())->GetColorTexture();
		}

		SetRenderTargetTextures(colors, std::min(count, MaxColorTargets), nil);
	}

	void GraphicsDeviceMetal::SetRenderTargetsWithDepthStencil(RenderTexturePtr* renderTargets, const uint32 count, RenderTexturePtr depthStencilRT)
	{
		id<MTLTexture> colors[MaxColorTargets] = {};
		for (uint32 i = 0; i < count && i < MaxColorTargets; ++i)
		{
			colors[i] = static_cast<RenderTextureMetal*>(renderTargets[i].get())->GetColorTexture();
		}

		id<MTLTexture> depth = depthStencilRT ? static_cast<RenderTextureMetal*>(depthStencilRT.get())->GetDepthTexture() : nil;
		SetRenderTargetTextures(colors, std::min(count, MaxColorTargets), depth);
	}

	void GraphicsDeviceMetal::ClearRenderTexture(id<MTLTexture> color, id<MTLTexture> depth, const ClearFlags flags)
	{
		const auto bits = static_cast<uint32>(flags);
		const bool clearColor = (bits & static_cast<uint32>(ClearFlags::Color)) != 0 && color;
		const bool clearDepth = (bits & static_cast<uint32>(ClearFlags::Depth)) != 0 && depth;
		const bool clearStencil = (bits & static_cast<uint32>(ClearFlags::Stencil)) != 0 && depth && HasStencil(depth.pixelFormat);

		if (!clearColor && !clearDepth && !clearStencil)
		{
			return;
		}

		int32 colorIndex = -1;
		for (uint32 i = 0; i < m_colorTargetCount; ++i)
		{
			if (color && m_colorTargets[i] == color)
			{
				colorIndex = static_cast<int32>(i);
			}
		}

		const bool colorBound = !clearColor || colorIndex >= 0;
		const bool depthBound = (!clearDepth && !clearStencil) || depth == m_depthTarget;
		if (!colorBound || !depthBound)
		{
			ExecuteStandaloneClear(clearColor ? color : nil, (clearDepth || clearStencil) ? depth : nil, flags);
			return;
		}

		// The clear becomes the load action of the next pass on these targets, so whatever was
		// drawn so far has to be finished first.
		EndRenderEncoder();

		if (clearColor)
		{
			m_pendingClear.color[colorIndex] = true;
		}
		m_pendingClear.depth |= clearDepth;
		m_pendingClear.stencil |= clearStencil;
	}

	void GraphicsDeviceMetal::ExecuteStandaloneClear(id<MTLTexture> color, id<MTLTexture> depth, const ClearFlags flags)
	{
		EndEncoders();

		const auto bits = static_cast<uint32>(flags);
		MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
		if (color)
		{
			pass.colorAttachments[0].texture = color;
			pass.colorAttachments[0].loadAction = MTLLoadActionClear;
			pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
			pass.colorAttachments[0].storeAction = MTLStoreActionStore;
		}

		if (depth)
		{
			pass.depthAttachment.texture = depth;
			pass.depthAttachment.loadAction = (bits & static_cast<uint32>(ClearFlags::Depth)) ? MTLLoadActionClear : MTLLoadActionLoad;
			pass.depthAttachment.clearDepth = 1.0;
			pass.depthAttachment.storeAction = MTLStoreActionStore;
			if (HasStencil(depth.pixelFormat))
			{
				pass.stencilAttachment.texture = depth;
				pass.stencilAttachment.loadAction = (bits & static_cast<uint32>(ClearFlags::Stencil)) ? MTLLoadActionClear : MTLLoadActionLoad;
				pass.stencilAttachment.clearStencil = 0;
				pass.stencilAttachment.storeAction = MTLStoreActionStore;
			}
		}

		id<MTLRenderCommandEncoder> encoder = [GetCommandBuffer() renderCommandEncoderWithDescriptor:pass];
		[encoder endEncoding];
	}

	void GraphicsDeviceMetal::ForgetTexture(id<MTLTexture> texture)
	{
		bool bound = texture == m_depthTarget;
		for (uint32 i = 0; i < m_colorTargetCount; ++i)
		{
			bound |= m_colorTargets[i] == texture;
		}

		if (bound)
		{
			EndEncoders();
			for (uint32 i = 0; i < m_colorTargetCount; ++i)
			{
				if (m_colorTargets[i] == texture)
				{
					m_colorTargets[i] = nil;
				}
			}
			if (m_depthTarget == texture)
			{
				m_depthTarget = nil;
			}
		}
	}

	void GraphicsDeviceMetal::Clear(const ClearFlags flags)
	{
		Reset();

		m_autoCreatedWindow->Activate();
		m_autoCreatedWindow->Clear(flags);
	}

	// ----------------------------------------------------------------------------------------
	// Resource creation

	VertexBufferPtr GraphicsDeviceMetal::CreateVertexBuffer(const size_t vertexCount, const size_t vertexSize, const BufferUsage usage, const void* initialData)
	{
		return std::make_shared<VertexBufferMetal>(*this, vertexCount, vertexSize, usage, initialData);
	}

	IndexBufferPtr GraphicsDeviceMetal::CreateIndexBuffer(const size_t indexCount, const IndexBufferSize indexSize, const BufferUsage usage, const void* initialData)
	{
		return std::make_shared<IndexBufferMetal>(*this, indexCount, indexSize, usage, initialData);
	}

	ConstantBufferPtr GraphicsDeviceMetal::CreateConstantBuffer(const size_t size, const void* initialData)
	{
		return std::make_shared<ConstantBufferMetal>(*this, size, initialData);
	}

	StructuredBufferPtr GraphicsDeviceMetal::CreateStructuredBuffer(const size_t elementSize, const size_t elementCount, const void* initialData)
	{
		return std::make_shared<StructuredBufferMetal>(*this, elementSize, elementCount, initialData);
	}

	ShaderPtr GraphicsDeviceMetal::CreateShader(const ShaderType type, const void* shaderCode, const size_t shaderCodeSize)
	{
		metal_shader_blob::Header header {};
		std::string_view source;
		if (!metal_shader_blob::Unpack(shaderCode, shaderCodeSize, header, source))
		{
			static bool s_warned = false;
			if (!s_warned)
			{
				s_warned = true;
				WLOG("Ignoring shader without Metal code (" << shaderCodeSize << " bytes). Materials need to be baked for Metal (metal_material_baker).");
			}
			return nullptr;
		}

		const size_t sourceHash = std::hash<std::string_view>()(source);
		id<MTLFunction> function = nil;
		if (const auto it = m_functionCache.find(sourceHash); it != m_functionCache.end())
		{
			function = it->second;
		}
		else
		{
			MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
			options.languageVersion = MTLLanguageVersion2_3;

			NSError* error = nil;
			NSString* sourceString = [[NSString alloc] initWithBytes:source.data() length:source.size() encoding:NSUTF8StringEncoding];
			id<MTLLibrary> library = [m_device newLibraryWithSource:sourceString options:options error:&error];
			if (!library)
			{
				ELOG("Failed to compile Metal shader: " << (error ? [[error localizedDescription] UTF8String] : "unknown error"));
				return nullptr;
			}

			function = [library newFunctionWithName:@"main0"];
			if (!function)
			{
				ELOG("Metal shader has no entry point main0");
				return nullptr;
			}

			m_functionCache[sourceHash] = function;
		}

		return std::make_unique<ShaderMetal>(*this, type, function, header.threadgroupSize);
	}

	TexturePtr GraphicsDeviceMetal::CreateTexture(const uint16 width, const uint16 height, const BufferUsage usage)
	{
		return std::make_shared<TextureMetal>(*this, width, height, usage);
	}

	RenderWindowPtr GraphicsDeviceMetal::CreateRenderWindow(std::string name, const uint16 width, const uint16 height, const bool fullScreen)
	{
		return std::make_shared<RenderWindowMetal>(*this, std::move(name), width, height, fullScreen);
	}

	RenderTexturePtr GraphicsDeviceMetal::CreateRenderTexture(std::string name, const uint16 width, const uint16 height, const RenderTextureFlags flags, const PixelFormat colorFormat, const PixelFormat depthFormat)
	{
		return std::make_shared<RenderTextureMetal>(*this, std::move(name), width, height, flags, colorFormat, depthFormat);
	}

	OcclusionQueryPtr GraphicsDeviceMetal::CreateOcclusionQuery()
	{
		return nullptr;
	}

	VolumeTexturePtr GraphicsDeviceMetal::CreateVolumeTexture(const uint16 width, const uint16 height, const uint16 depth, const VolumeFormat format, const bool writable)
	{
		auto texture = std::make_shared<VolumeTextureMetal>(*this, width, height, depth, format, writable);
		return texture->IsValid() ? texture : nullptr;
	}

	TextureArrayPtr GraphicsDeviceMetal::CreateTextureArray(const uint16 width, const uint16 height, const uint16 layers, const TextureArrayFormat format, const bool writable, const bool mipmapped)
	{
		auto texture = std::make_shared<TextureArrayMetal>(*this, width, height, layers, format, writable, mipmapped);
		return texture->IsValid() ? texture : nullptr;
	}

	SamplerStatePtr GraphicsDeviceMetal::CreateSamplerState(const SamplerDesc& desc)
	{
		return std::make_shared<SamplerStateMetal>(*this, desc);
	}

	std::unique_ptr<MaterialCompiler> GraphicsDeviceMetal::CreateMaterialCompiler()
	{
		return std::make_unique<MaterialCompilerNull>();
	}

	std::unique_ptr<ShaderCompiler> GraphicsDeviceMetal::CreateShaderCompiler()
	{
		return std::make_unique<ShaderCompilerNull>();
	}

	id<MTLSamplerState> GraphicsDeviceMetal::GetSampler(MTLSamplerDescriptor* descriptor)
	{
		uint64 hash = 1469598103934665603ull;
		hash = HashValue(descriptor.minFilter, hash);
		hash = HashValue(descriptor.magFilter, hash);
		hash = HashValue(descriptor.mipFilter, hash);
		hash = HashValue(descriptor.maxAnisotropy, hash);
		hash = HashValue(descriptor.sAddressMode, hash);
		hash = HashValue(descriptor.tAddressMode, hash);
		hash = HashValue(descriptor.rAddressMode, hash);
		hash = HashValue(descriptor.borderColor, hash);
		hash = HashValue(descriptor.compareFunction, hash);

		if (const auto it = m_samplerStates.find(hash); it != m_samplerStates.end())
		{
			return it->second;
		}

		id<MTLSamplerState> sampler = [m_device newSamplerStateWithDescriptor:descriptor];
		m_samplerStates[hash] = sampler;
		return sampler;
	}

	// ----------------------------------------------------------------------------------------
	// Binding

	GraphicsDeviceMetal::StageBindings& GraphicsDeviceMetal::GetStage(const ShaderType stage)
	{
		return m_stages[StageIndex(stage)];
	}

	void GraphicsDeviceMetal::SetShader(ShaderMetal* shader)
	{
		switch (shader->GetType())
		{
		case ShaderType::VertexShader:
			m_vertexShader = shader;
			break;
		case ShaderType::PixelShader:
			m_pixelShader = shader;
			break;
		case ShaderType::ComputeShader:
			m_computeShader = shader;
			break;
		default:
			break;
		}
	}

	void GraphicsDeviceMetal::ForgetShader(const ShaderMetal* shader)
	{
		if (m_vertexShader == shader)
		{
			m_vertexShader = nullptr;
		}
		if (m_pixelShader == shader)
		{
			m_pixelShader = nullptr;
		}
		if (m_computeShader == shader)
		{
			m_computeShader = nullptr;
		}
	}

	void GraphicsDeviceMetal::BindNullPixelShader()
	{
		m_pixelShader = nullptr;
	}

	void GraphicsDeviceMetal::SetVertexBuffer(const uint16 slot, VertexBufferMetal* buffer)
	{
		if (slot >= metal_bindings::MaxVertexStreams)
		{
			ELOG("Vertex stream " << slot << " exceeds the supported number of vertex streams");
			return;
		}

		m_vertexStreams[slot] = buffer;
		m_vertexStreamsDirty = true;
	}

	void GraphicsDeviceMetal::ForgetVertexBuffer(const VertexBufferMetal* buffer)
	{
		for (auto& stream : m_vertexStreams)
		{
			if (stream == buffer)
			{
				stream = nullptr;
			}
		}
	}

	void GraphicsDeviceMetal::SetIndexBuffer(IndexBufferMetal* buffer)
	{
		m_indexBuffer = buffer;
		m_indexCount = static_cast<uint32>(buffer->GetIndexCount());
	}

	void GraphicsDeviceMetal::ForgetIndexBuffer(const IndexBufferMetal* buffer)
	{
		if (m_indexBuffer == buffer)
		{
			m_indexBuffer = nullptr;
		}
	}

	void GraphicsDeviceMetal::BindConstantBuffer(const ShaderType stage, const uint32 slot, ConstantBufferMetal* buffer)
	{
		if (slot >= metal_bindings::MaxConstantBuffers)
		{
			return;
		}

		StageBindings& bindings = GetStage(stage);
		if (bindings.constantBuffers[slot] != buffer)
		{
			bindings.constantBuffers[slot] = buffer;
			bindings.dirtyConstantBuffers |= 1u << slot;
		}
	}

	void GraphicsDeviceMetal::ForgetConstantBuffer(const ConstantBufferMetal* buffer)
	{
		for (auto& stage : m_stages)
		{
			for (uint32 i = 0; i < metal_bindings::MaxConstantBuffers; ++i)
			{
				if (stage.constantBuffers[i] == buffer)
				{
					stage.constantBuffers[i] = nullptr;
					stage.dirtyConstantBuffers |= 1u << i;
				}
			}
		}
	}

	void GraphicsDeviceMetal::BindStageBuffer(const ShaderType stage, const uint32 index, RenamedMetalBuffer* buffer)
	{
		if (index >= MaxBufferSlots)
		{
			return;
		}

		StageBindings& bindings = GetStage(stage);
		bindings.buffers[index] = buffer;
		bindings.dirtyBuffers |= 1u << index;
	}

	void GraphicsDeviceMetal::ForgetStageBuffer(const RenamedMetalBuffer* buffer)
	{
		for (auto& stage : m_stages)
		{
			for (uint32 i = 0; i < MaxBufferSlots; ++i)
			{
				if (stage.buffers[i] == buffer)
				{
					stage.buffers[i] = nullptr;
				}
			}
		}
	}

	void GraphicsDeviceMetal::BindStageTexture(const ShaderType stage, const uint32 index, id<MTLTexture> texture)
	{
		if (index >= MaxTextureSlots)
		{
			return;
		}

		StageBindings& bindings = GetStage(stage);
		if (bindings.textures[index] != texture)
		{
			bindings.textures[index] = texture;
			bindings.dirtyTextures |= 1u << index;
		}
	}

	void GraphicsDeviceMetal::BindStageSampler(const ShaderType stage, const uint32 index, id<MTLSamplerState> sampler)
	{
		if (index >= metal_bindings::MaxSamplers)
		{
			return;
		}

		StageBindings& bindings = GetStage(stage);
		if (bindings.samplers[index] != sampler)
		{
			bindings.samplers[index] = sampler;
			bindings.dirtySamplers |= 1u << index;
		}
	}

	void GraphicsDeviceMetal::BindTexture(const TexturePtr texture, const ShaderType shader, const uint32 slot)
	{
		if (!texture)
		{
			return;
		}

		texture->Bind(shader, slot);

		// D3D11 binds the device's current sampler to the pixel stage slot of the texture.
		BindStageSampler(ShaderType::PixelShader, slot, GetCurrentSampler());
	}

	id<MTLSamplerState> GraphicsDeviceMetal::GetCurrentSampler()
	{
		if (m_samplerDirty || !m_currentSampler)
		{
			MTLSamplerDescriptor* descriptor = [[MTLSamplerDescriptor alloc] init];
			descriptor.minFilter = m_samplerMinMag;
			descriptor.magFilter = m_samplerMinMag;
			descriptor.mipFilter = m_samplerMip;
			descriptor.maxAnisotropy = m_samplerAnisotropy;
			descriptor.sAddressMode = m_samplerAddress[0];
			descriptor.tAddressMode = m_samplerAddress[1];
			descriptor.rAddressMode = m_samplerAddress[2];
			descriptor.borderColor = MTLSamplerBorderColorTransparentBlack;
			m_currentSampler = GetSampler(descriptor);
			m_samplerDirty = false;
		}

		return m_currentSampler;
	}

	// ----------------------------------------------------------------------------------------
	// Fixed function state

	void GraphicsDeviceMetal::Reset()
	{
		PROFILE_SCOPE("Metal::Reset");

		m_vertexFormat = VertexFormat::Last;
		m_layoutFromFormat = false;
		m_blendMode = BlendMode::Undefined;
		m_topologyType = TopologyType::Undefined;

		m_depthBias = 0.0f;
		m_slopeScaledDepthBias = 0.0f;
		m_depthBiasClamp = 0.0f;
		m_rasterStateDirty = true;

		m_vertexShader = nullptr;
		m_pixelShader = nullptr;
		m_lastBoundMaterial = nullptr;

		if (m_worldMatrixDirty || m_viewMatrixDirty)
		{
			m_transform[World] = Matrix4::Identity;
			m_transform[View] = Matrix4::Identity;
			m_transform[Projection] = Matrix4::Identity;
			m_inverseView = Matrix4::Identity;
			m_inverseProj = Matrix4::Identity;
			m_worldMatrixDirty = true;
			m_viewMatrixDirty = true;
			UpdateMatrixBuffers();
		}

		BindConstantBuffer(ShaderType::VertexShader, 0, static_cast<ConstantBufferMetal*>(m_objectMatrixBuffer.get()));
		BindConstantBuffer(ShaderType::PixelShader, 0, static_cast<ConstantBufferMetal*>(m_objectMatrixBuffer.get()));
		BindConstantBuffer(ShaderType::VertexShader, 12, static_cast<ConstantBufferMetal*>(m_viewMatrixBuffer.get()));
		BindConstantBuffer(ShaderType::PixelShader, 12, static_cast<ConstantBufferMetal*>(m_viewMatrixBuffer.get()));
	}

	void GraphicsDeviceMetal::SetClearColor(const uint32 clearColor)
	{
		GraphicsDevice::SetClearColor(clearColor);

		m_clearColorFloat[0] = static_cast<float>((clearColor >> 16) & 0xFF) / 255.0f;
		m_clearColorFloat[1] = static_cast<float>((clearColor >> 8) & 0xFF) / 255.0f;
		m_clearColorFloat[2] = static_cast<float>(clearColor & 0xFF) / 255.0f;
		m_clearColorFloat[3] = 1.0f;
	}

	Matrix4 GraphicsDeviceMetal::MakeProjectionMatrix(const Radian& fovY, const float aspect, const float nearPlane, const float farPlane)
	{
		// Metal uses the same clip space as D3D11 (depth 0 to 1), so this matches the D3D11 device.
		Matrix4 dest = Matrix4::Zero;

		const float theta = fovY.GetValueRadians() * 0.5f;
		const float h = 1 / std::tan(theta);
		const float w = h / aspect;

		const float q = farPlane / (farPlane - nearPlane);
		const float qn = -q * nearPlane;

		dest[0][0] = w;
		dest[1][1] = h;
		dest[2][2] = -q;
		dest[3][2] = -1.0f;
		dest[2][3] = qn;

		return dest;
	}

	Matrix4 GraphicsDeviceMetal::MakeOrthographicMatrix(const float left, const float top, const float right, const float bottom, const float nearPlane, const float farPlane)
	{
		const float invW = 1.0f / (right - left);
		const float invH = 1.0f / (top - bottom);
		const float invD = 1.0f / (farPlane - nearPlane);

		Matrix4 result = Matrix4::Zero;
		result[0][0] = 2.0f * invW;
		result[0][3] = -(right + left) * invW;
		result[1][1] = 2.0f * invH;
		result[1][3] = -(top + bottom) * invH;
		result[2][2] = invD;
		result[2][3] = -nearPlane * invD;
		result[3][3] = 1.0f;

		return result;
	}

	void GraphicsDeviceMetal::SetDepthBias(const float bias)
	{
		m_depthBias = bias;
		m_rasterStateDirty = true;
	}

	void GraphicsDeviceMetal::SetSlopeScaledDepthBias(const float bias)
	{
		m_slopeScaledDepthBias = bias;
		m_rasterStateDirty = true;
	}

	void GraphicsDeviceMetal::SetDepthBiasClamp(const float bias)
	{
		m_depthBiasClamp = bias;
		m_rasterStateDirty = true;
	}

	void GraphicsDeviceMetal::SetTopologyType(const TopologyType type)
	{
		GraphicsDevice::SetTopologyType(type);
	}

	void GraphicsDeviceMetal::SetBlendMode(const BlendMode blendMode)
	{
		GraphicsDevice::SetBlendMode(blendMode);
	}

	void GraphicsDeviceMetal::CaptureState()
	{
		GraphicsDevice::CaptureState();

		m_restoreInverseView = m_inverseView;
		m_restoreInverseProj = m_inverseProj;
	}

	void GraphicsDeviceMetal::RestoreState()
	{
		GraphicsDevice::RestoreState();

		m_inverseView = m_restoreInverseView;
		m_inverseProj = m_restoreInverseProj;

		m_samplerDirty = true;
		m_worldMatrixDirty = true;
		m_viewMatrixDirty = true;
		m_rasterStateDirty = true;
		m_viewportDirty = true;
		m_vertexFormat = VertexFormat::Last;
		m_layoutFromFormat = false;
	}

	void GraphicsDeviceMetal::SetTransformMatrix(const TransformType type, Matrix4 const& matrix)
	{
		if (GetTransformMatrix(type) == matrix)
		{
			return;
		}

		GraphicsDevice::SetTransformMatrix(type, matrix);

		if (type == View)
		{
			m_inverseView = m_transform[View].InverseAffine();
			m_viewMatrixDirty = true;
		}
		else if (type == Projection)
		{
			m_inverseProj = m_transform[Projection].Inverse();
			m_viewMatrixDirty = true;
		}
		else
		{
			m_worldMatrixDirty = true;
		}
	}

	void GraphicsDeviceMetal::UpdateMatrixBuffers()
	{
		if (m_worldMatrixDirty)
		{
			m_worldMatrixDirty = false;
			m_objectMatrixBuffer->Update(&m_transform[World]);
		}

		if (m_viewMatrixDirty)
		{
			m_viewMatrixDirty = false;
			Matrix4 viewMatrices[4] = { m_transform[View], m_transform[Projection], m_inverseView, m_inverseProj };
			m_viewMatrixBuffer->Update(viewMatrices);
		}
	}

	void GraphicsDeviceMetal::SetViewport(const int32 x, const int32 y, const int32 w, const int32 h, const float minZ, const float maxZ)
	{
		GraphicsDevice::SetViewport(x, y, w, h, minZ, maxZ);
		m_viewportDirty = true;
	}

	void GraphicsDeviceMetal::SetClipRect(const int32 x, const int32 y, const int32 w, const int32 h)
	{
		m_scissorEnabled = true;
		m_scissorRect.x = static_cast<NSUInteger>(std::max(0, x));
		m_scissorRect.y = static_cast<NSUInteger>(std::max(0, y));
		m_scissorRect.width = static_cast<NSUInteger>(std::max(0, w + std::min(0, x)));
		m_scissorRect.height = static_cast<NSUInteger>(std::max(0, h + std::min(0, y)));
		m_rasterStateDirty = true;
	}

	void GraphicsDeviceMetal::ResetClipRect()
	{
		m_scissorEnabled = false;
		m_rasterStateDirty = true;
	}

	void GraphicsDeviceMetal::SetFillMode(const FillMode mode)
	{
		if (m_fillMode != mode)
		{
			GraphicsDevice::SetFillMode(mode);
			m_rasterStateDirty = true;
		}
	}

	void GraphicsDeviceMetal::SetFaceCullMode(const FaceCullMode mode)
	{
		if (m_cullMode != mode)
		{
			GraphicsDevice::SetFaceCullMode(mode);
			m_rasterStateDirty = true;
		}
	}

	void GraphicsDeviceMetal::SetTextureAddressMode(const TextureAddressMode modeU, const TextureAddressMode modeV, const TextureAddressMode modeW)
	{
		if (m_texAddressMode[0] == modeU && m_texAddressMode[1] == modeV && m_texAddressMode[2] == modeW)
		{
			return;
		}

		GraphicsDevice::SetTextureAddressMode(modeU, modeV, modeW);
		m_samplerAddress[0] = ToMetalAddress(modeU);
		m_samplerAddress[1] = ToMetalAddress(modeV);
		m_samplerAddress[2] = ToMetalAddress(modeW);
		m_samplerDirty = true;
	}

	void GraphicsDeviceMetal::SetTextureFilter(const TextureFilter filter)
	{
		if (m_texFilter == filter)
		{
			return;
		}

		GraphicsDevice::SetTextureFilter(filter);
		ApplyEffectiveTextureFilter();
	}

	void GraphicsDeviceMetal::ApplyEffectiveTextureFilter()
	{
		TextureFilter cap = m_textureFilterCap;
		if (m_maxAnisotropy <= 1 && cap == TextureFilter::Anisotropic)
		{
			cap = TextureFilter::Trilinear;
		}

		const TextureFilter effective = static_cast<int>(m_texFilter) > static_cast<int>(cap) ? cap : m_texFilter;

		MTLSamplerMinMagFilter minMag = MTLSamplerMinMagFilterLinear;
		MTLSamplerMipFilter mip = MTLSamplerMipFilterLinear;
		uint32 anisotropy = 1;
		switch (effective)
		{
		case TextureFilter::None:
			minMag = MTLSamplerMinMagFilterNearest;
			mip = MTLSamplerMipFilterNearest;
			break;
		case TextureFilter::Bilinear:
			mip = MTLSamplerMipFilterNearest;
			break;
		case TextureFilter::Trilinear:
			break;
		case TextureFilter::Anisotropic:
			anisotropy = std::clamp<uint32>(m_maxAnisotropy, 1, 16);
			break;
		}

		if (minMag != m_samplerMinMag || mip != m_samplerMip || anisotropy != m_samplerAnisotropy)
		{
			m_samplerMinMag = minMag;
			m_samplerMip = mip;
			m_samplerAnisotropy = anisotropy;
			m_samplerDirty = true;
		}
	}

	void GraphicsDeviceMetal::SetMaxAnisotropy(const uint32 maxAnisotropy)
	{
		m_maxAnisotropy = std::clamp<uint32>(maxAnisotropy, 1, 16);
		ApplyEffectiveTextureFilter();
	}

	void GraphicsDeviceMetal::SetTextureFilterCap(const TextureFilter maxFilter)
	{
		m_textureFilterCap = maxFilter;
		ApplyEffectiveTextureFilter();
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

	void GraphicsDeviceMetal::SetStencilWriteEnabled(const bool enable)
	{
		m_stencilWrite = enable;
	}

	void GraphicsDeviceMetal::SetStencilRef(const uint8 reference)
	{
		if (m_stencilRef != reference)
		{
			m_stencilRef = reference;
			m_rasterStateDirty = true;
		}
	}

	void GraphicsDeviceMetal::SetVSyncEnabled(const bool enable)
	{
		m_vsync = enable;
		if (m_mainWindow)
		{
			m_mainWindow->ApplyVSync();
		}
	}

	// ----------------------------------------------------------------------------------------
	// Vertex layouts

	void GraphicsDeviceMetal::SetVertexFormat(const VertexFormat format)
	{
		if (m_layoutFromFormat && m_vertexFormat == format)
		{
			return;
		}

		SetVertexLayoutFromFormat(format);

		// Like D3D11, switching to a fixed vertex format also binds the matching built-in shaders.
		if (const auto it = m_formatVertexShaders.find(format); it != m_formatVertexShaders.end() && it->second)
		{
			it->second->Set();
		}
		if (const auto it = m_formatPixelShaders.find(format); it != m_formatPixelShaders.end() && it->second)
		{
			it->second->Set();
		}
	}

	void GraphicsDeviceMetal::SetVertexLayoutFromFormat(const VertexFormat format)
	{
		m_vertexFormat = format;
		m_layoutFromFormat = true;

		auto& elements = m_vertexLayout.elements;
		elements.clear();
		elements.push_back({ VertexElementSemantic::Position, 0, MTLVertexFormatFloat3, 0, 0 });

		switch (format)
		{
		case VertexFormat::PosColor:
			elements.push_back({ VertexElementSemantic::Diffuse, 0, MTLVertexFormatUChar4Normalized_BGRA, 0, 12 });
			break;
		case VertexFormat::PosColorTex1:
			elements.push_back({ VertexElementSemantic::Diffuse, 0, MTLVertexFormatUChar4Normalized_BGRA, 0, 12 });
			elements.push_back({ VertexElementSemantic::TextureCoordinate, 0, MTLVertexFormatFloat2, 0, 16 });
			break;
		case VertexFormat::PosColorNormal:
			elements.push_back({ VertexElementSemantic::Diffuse, 0, MTLVertexFormatUChar4Normalized_BGRA, 0, 12 });
			elements.push_back({ VertexElementSemantic::Normal, 0, MTLVertexFormatFloat3, 0, 16 });
			break;
		case VertexFormat::PosColorNormalTex1:
			elements.push_back({ VertexElementSemantic::Diffuse, 0, MTLVertexFormatUChar4Normalized_BGRA, 0, 12 });
			elements.push_back({ VertexElementSemantic::Normal, 0, MTLVertexFormatFloat3, 0, 16 });
			elements.push_back({ VertexElementSemantic::TextureCoordinate, 0, MTLVertexFormatFloat2, 0, 28 });
			break;
		case VertexFormat::PosColorNormalBinormalTangentTex1:
			elements.push_back({ VertexElementSemantic::Diffuse, 0, MTLVertexFormatUChar4Normalized_BGRA, 0, 12 });
			elements.push_back({ VertexElementSemantic::Normal, 0, MTLVertexFormatFloat3, 0, 16 });
			elements.push_back({ VertexElementSemantic::Binormal, 0, MTLVertexFormatFloat3, 0, 28 });
			elements.push_back({ VertexElementSemantic::Tangent, 0, MTLVertexFormatFloat3, 0, 40 });
			elements.push_back({ VertexElementSemantic::TextureCoordinate, 0, MTLVertexFormatFloat2, 0, 52 });
			break;
		default:
			break;
		}

		m_vertexLayout.instanced = false;
		m_vertexLayout.instanceStream = 0;
		m_vertexLayout.hash = HashValue(static_cast<uint32>(format), 0x5A5A5A5Aull);
	}

	void GraphicsDeviceMetal::SetVertexLayoutFromDeclaration(VertexDeclaration& declaration, const bool instanced, const uint16 instanceStream)
	{
		m_vertexFormat = VertexFormat::Last;
		m_layoutFromFormat = false;

		const size_t declarationHash = declaration.GetHash();
		CachedDeclarationLayout& cached = m_declarationLayouts[&declaration];
		if (cached.declarationHash != declarationHash || cached.elements.empty())
		{
			cached.declarationHash = declarationHash;
			cached.elements.clear();
			for (const VertexElement& element : declaration.GetElements())
			{
				cached.elements.push_back({ element.GetSemantic(), element.GetIndex(), ToMetalVertexFormat(element.GetType()), element.GetSource(), element.GetOffset() });
			}
		}

		m_vertexLayout.elements = cached.elements;
		m_vertexLayout.instanced = instanced;
		m_vertexLayout.instanceStream = instanceStream;

		if (instanced)
		{
			// The instance stream carries a world matrix (TEXCOORD8 to 11) and a tint (TEXCOORD12),
			// matching the instanced input layout of the D3D11 backend.
			for (uint16 row = 0; row < 4; ++row)
			{
				m_vertexLayout.elements.push_back({ VertexElementSemantic::TextureCoordinate, static_cast<uint16>(8 + row), MTLVertexFormatFloat4, instanceStream, row * 16u });
			}
			m_vertexLayout.elements.push_back({ VertexElementSemantic::TextureCoordinate, 12, MTLVertexFormatFloat4, instanceStream, 64 });
		}

		uint64 hash = HashValue(declarationHash, 0x1234567ull);
		hash = HashValue(instanced, hash);
		hash = HashValue(instanceStream, hash);
		m_vertexLayout.hash = hash;
	}

	MTLVertexDescriptor* GraphicsDeviceMetal::BuildVertexDescriptor(id<MTLFunction> vertexFunction)
	{
		MTLVertexDescriptor* descriptor = [MTLVertexDescriptor vertexDescriptor];

		bool usesPlaceholder = false;
		uint32 usedStreams = 0;

		for (MTLVertexAttribute* attribute in vertexFunction.vertexAttributes)
		{
			if (!attribute.active)
			{
				continue;
			}

			std::string semantic;
			uint32 semanticIndex = 0;
			const VertexLayoutElement* match = nullptr;
			if (ParseAttributeName([attribute.name UTF8String], semantic, semanticIndex))
			{
				for (const auto& element : m_vertexLayout.elements)
				{
					if (element.index == semanticIndex && semantic == SemanticName(element.semantic))
					{
						match = &element;
						break;
					}
				}
			}

			MTLVertexAttributeDescriptor* attributeDescriptor = descriptor.attributes[attribute.attributeIndex];
			if (std::getenv("MMO_METAL_DEBUG_LAYOUTS"))
			{
				ILOG("  attribute " << [attribute.name UTF8String] << " @" << attribute.attributeIndex << " -> "
					<< (match ? ("stream " + std::to_string(match->stream) + " offset " + std::to_string(match->offset) + " format " + std::to_string(match->format)) : std::string("placeholder")));
			}
			if (match && match->format != MTLVertexFormatInvalid)
			{
				attributeDescriptor.format = match->format;
				attributeDescriptor.offset = match->offset;
				attributeDescriptor.bufferIndex = metal_bindings::VertexBufferTop - match->stream;
				usedStreams |= 1u << match->stream;
			}
			else
			{
				attributeDescriptor.format = PlaceholderVertexFormat(attribute.attributeType);
				attributeDescriptor.offset = 0;
				attributeDescriptor.bufferIndex = NullVertexBufferIndex;
				usesPlaceholder = true;
			}
		}

		for (uint32 stream = 0; stream < metal_bindings::MaxVertexStreams; ++stream)
		{
			if ((usedStreams & (1u << stream)) == 0)
			{
				continue;
			}

			MTLVertexBufferLayoutDescriptor* layout = descriptor.layouts[metal_bindings::VertexBufferTop - stream];
			const VertexBufferMetal* buffer = m_vertexStreams[stream];
			layout.stride = buffer ? std::max<uint32>(4, buffer->GetVertexSize()) : 16;
			if (m_vertexLayout.instanced && stream == m_vertexLayout.instanceStream)
			{
				layout.stepFunction = MTLVertexStepFunctionPerInstance;
				layout.stepRate = 1;
			}
			else
			{
				layout.stepFunction = MTLVertexStepFunctionPerVertex;
				layout.stepRate = 1;
			}
		}

		if (usesPlaceholder)
		{
			MTLVertexBufferLayoutDescriptor* layout = descriptor.layouts[NullVertexBufferIndex];
			layout.stride = 16;
			layout.stepFunction = MTLVertexStepFunctionConstant;
			layout.stepRate = 0;
		}

		return descriptor;
	}

	// ----------------------------------------------------------------------------------------
	// Pipelines

	void GraphicsDeviceMetal::CollectBindings(NSArray<id<MTLBinding>>* bindings, const uint8 stage, uint32& usedBuffers, uint32& usedSamplers, std::vector<ShaderTextureUse>& textures)
	{
		for (id<MTLBinding> binding in bindings)
		{
			if (!binding.used)
			{
				continue;
			}

			switch (binding.type)
			{
			case MTLBindingTypeBuffer:
				if (binding.index < 32)
				{
					usedBuffers |= 1u << binding.index;
				}
				break;
			case MTLBindingTypeSampler:
				if (binding.index < 32)
				{
					usedSamplers |= 1u << binding.index;
				}
				break;
			case MTLBindingTypeTexture:
			{
				id<MTLTextureBinding> textureBinding = (id<MTLTextureBinding>)binding;
				ShaderTextureUse use {};
				use.stage = stage;
				use.index = static_cast<uint8>(binding.index);
				use.type = textureBinding.textureType;
				use.dataType = textureBinding.textureDataType;
				use.depth = textureBinding.isDepthTexture;
				textures.push_back(use);
				break;
			}
			default:
				break;
			}
		}
	}

	const GraphicsDeviceMetal::PipelineEntry* GraphicsDeviceMetal::GetPipelineState()
	{
		if (!m_vertexShader)
		{
			return nullptr;
		}

		const id<MTLFunction> vertexFunction = m_vertexShader->GetFunction();
		const id<MTLFunction> fragmentFunction = m_pixelShader ? m_pixelShader->GetFunction() : nil;

		uint64 hash = HashValue((__bridge const void*)vertexFunction, 1469598103934665603ull);
		hash = HashValue((__bridge const void*)fragmentFunction, hash);
		hash = HashValue(m_vertexLayout.hash, hash);
		for (uint32 stream = 0; stream < metal_bindings::MaxVertexStreams; ++stream)
		{
			hash = HashValue(m_vertexStreams[stream] ? m_vertexStreams[stream]->GetVertexSize() : 0u, hash);
		}
		for (uint32 i = 0; i < MaxColorTargets; ++i)
		{
			hash = HashValue(i < m_colorTargetCount && m_colorTargets[i] ? m_colorTargets[i].pixelFormat : MTLPixelFormatInvalid, hash);
		}
		hash = HashValue(m_depthTarget ? m_depthTarget.pixelFormat : MTLPixelFormatInvalid, hash);
		const bool alphaBlend = m_blendMode == BlendMode::Alpha;
		hash = HashValue(alphaBlend, hash);

		if (const auto it = m_pipelineStates.find(hash); it != m_pipelineStates.end())
		{
			return it->second.state ? &it->second : nullptr;
		}

		MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
		descriptor.vertexFunction = vertexFunction;
		descriptor.fragmentFunction = fragmentFunction;
		descriptor.vertexDescriptor = BuildVertexDescriptor(vertexFunction);

		for (uint32 i = 0; i < m_colorTargetCount; ++i)
		{
			if (!m_colorTargets[i])
			{
				continue;
			}

			MTLRenderPipelineColorAttachmentDescriptor* attachment = descriptor.colorAttachments[i];
			attachment.pixelFormat = m_colorTargets[i].pixelFormat;
			attachment.writeMask = MTLColorWriteMaskAll;
			if (alphaBlend)
			{
				attachment.blendingEnabled = YES;
				attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
				attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
				attachment.rgbBlendOperation = MTLBlendOperationAdd;
				attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
				attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
				attachment.alphaBlendOperation = MTLBlendOperationAdd;
			}
		}

		if (m_depthTarget)
		{
			descriptor.depthAttachmentPixelFormat = m_depthTarget.pixelFormat;
			if (HasStencil(m_depthTarget.pixelFormat))
			{
				descriptor.stencilAttachmentPixelFormat = m_depthTarget.pixelFormat;
			}
		}

		NSError* error = nil;
		MTLAutoreleasedRenderPipelineReflection reflection = nil;
		id<MTLRenderPipelineState> state = [m_device newRenderPipelineStateWithDescriptor:descriptor options:MTLPipelineOptionBindingInfo reflection:&reflection error:&error];

		PipelineEntry& entry = m_pipelineStates[hash];
		if (!state)
		{
			ELOG("Failed to create Metal render pipeline: " << (error ? [[error localizedDescription] UTF8String] : "unknown error"));
			return nullptr;
		}

		entry.state = state;
		CollectBindings(reflection.vertexBindings, 0, entry.usedBuffers[0], entry.usedSamplers[0], entry.textures);
		CollectBindings(reflection.fragmentBindings, 1, entry.usedBuffers[1], entry.usedSamplers[1], entry.textures);

		// Vertex streams are bound from the device's own state, not as placeholders.
		for (uint32 stream = 0; stream <= metal_bindings::MaxVertexStreams; ++stream)
		{
			entry.usedBuffers[0] &= ~(1u << (metal_bindings::VertexBufferTop - stream));
		}

		return &entry;
	}

	id<MTLDepthStencilState> GraphicsDeviceMetal::GetDepthStencilState()
	{
		uint32 key = (m_depthEnabled ? 1u : 0u)
			| (m_depthEnabled && m_depthWrite ? 2u : 0u)
			| (static_cast<uint32>(m_depthComparison) << 2)
			| (m_stencilWrite ? 0x100u : 0u);

		if (const auto it = m_depthStencilStates.find(key); it != m_depthStencilStates.end())
		{
			return it->second;
		}

		MTLDepthStencilDescriptor* descriptor = [[MTLDepthStencilDescriptor alloc] init];
		descriptor.depthCompareFunction = m_depthEnabled ? ToMetalCompare(m_depthComparison) : MTLCompareFunctionAlways;
		descriptor.depthWriteEnabled = m_depthEnabled && m_depthWrite;

		if (m_stencilWrite)
		{
			// Tag mode (see GraphicsDevice::SetStencilWriteEnabled): a visible pixel takes the ref.
			MTLStencilDescriptor* stencil = [[MTLStencilDescriptor alloc] init];
			stencil.stencilCompareFunction = MTLCompareFunctionAlways;
			stencil.stencilFailureOperation = MTLStencilOperationKeep;
			stencil.depthFailureOperation = MTLStencilOperationKeep;
			stencil.depthStencilPassOperation = MTLStencilOperationReplace;
			stencil.readMask = 0xFF;
			stencil.writeMask = 0xFF;
			descriptor.frontFaceStencil = stencil;
			descriptor.backFaceStencil = stencil;
		}

		id<MTLDepthStencilState> state = [m_device newDepthStencilStateWithDescriptor:descriptor];
		m_depthStencilStates[key] = state;
		return state;
	}

	id<MTLTexture> GraphicsDeviceMetal::GetPlaceholderTexture(const ShaderTextureUse& use)
	{
		const uint64 key = (static_cast<uint64>(use.type) << 32) | (static_cast<uint64>(use.dataType) << 1) | (use.depth ? 1u : 0u);
		if (const auto it = m_placeholderTextures.find(key); it != m_placeholderTextures.end())
		{
			return it->second;
		}

		MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
		descriptor.textureType = use.type;
		descriptor.width = 1;
		descriptor.height = 1;
		descriptor.depth = 1;
		descriptor.storageMode = MTLStorageModePrivate;
		descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
		if (use.type == MTLTextureType2DArray || use.type == MTLTextureTypeCubeArray)
		{
			descriptor.arrayLength = 1;
		}

		if (use.depth)
		{
			descriptor.pixelFormat = MTLPixelFormatDepth32Float;
			descriptor.usage = MTLTextureUsageShaderRead;
		}
		else if (use.dataType == MTLDataTypeUInt || use.dataType == MTLDataTypeUShort)
		{
			descriptor.pixelFormat = MTLPixelFormatR32Uint;
		}
		else if (use.dataType == MTLDataTypeInt || use.dataType == MTLDataTypeShort)
		{
			descriptor.pixelFormat = MTLPixelFormatR32Sint;
		}
		else
		{
			descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
		}

		id<MTLTexture> texture = [m_device newTextureWithDescriptor:descriptor];
		m_placeholderTextures[key] = texture;
		return texture;
	}

	void GraphicsDeviceMetal::ApplyStageBindings(const ShaderType stage)
	{
		StageBindings& bindings = GetStage(stage);
		const bool vertex = stage == ShaderType::VertexShader;

		// Constant buffers: copy the current contents into the command stream when they changed.
		for (uint32 slot = 0; slot < metal_bindings::MaxConstantBuffers; ++slot)
		{
			ConstantBufferMetal* buffer = bindings.constantBuffers[slot];
			if (!buffer)
			{
				continue;
			}

			const bool dirty = (bindings.dirtyConstantBuffers & (1u << slot)) != 0 || bindings.constantBufferVersions[slot] != buffer->GetVersion();
			if (!dirty)
			{
				continue;
			}

			const NSUInteger size = buffer->GetSize();
			if (size <= MaxSetBytesSize)
			{
				if (vertex)
				{
					[m_renderEncoder setVertexBytes:buffer->GetData() length:size atIndex:slot];
				}
				else
				{
					[m_renderEncoder setFragmentBytes:buffer->GetData() length:size atIndex:slot];
				}
			}
			else
			{
				id<MTLBuffer> transient = nil;
				const NSUInteger offset = AllocateTransient(buffer->GetData(), size, transient);
				if (vertex)
				{
					[m_renderEncoder setVertexBuffer:transient offset:offset atIndex:slot];
				}
				else
				{
					[m_renderEncoder setFragmentBuffer:transient offset:offset atIndex:slot];
				}
			}

			bindings.constantBufferVersions[slot] = buffer->GetVersion();
		}
		bindings.dirtyConstantBuffers = 0;

		// Structured buffers are set for every draw: an update in between renames them.
		for (uint32 index = metal_bindings::MaxConstantBuffers; index < MaxBufferSlots; ++index)
		{
			if (!bindings.buffers[index])
			{
				continue;
			}

			if (vertex)
			{
				[m_renderEncoder setVertexBuffer:bindings.buffers[index]->Use() offset:0 atIndex:index];
			}
			else
			{
				[m_renderEncoder setFragmentBuffer:bindings.buffers[index]->Use() offset:0 atIndex:index];
			}
		}
		bindings.dirtyBuffers = 0;

		for (uint32 index = 0; index < MaxTextureSlots; ++index)
		{
			if ((bindings.dirtyTextures & (1u << index)) == 0 || !bindings.textures[index])
			{
				continue;
			}

			if (vertex)
			{
				[m_renderEncoder setVertexTexture:bindings.textures[index] atIndex:index];
			}
			else
			{
				[m_renderEncoder setFragmentTexture:bindings.textures[index] atIndex:index];
			}
		}
		bindings.dirtyTextures = 0;

		for (uint32 index = 0; index < metal_bindings::MaxSamplers; ++index)
		{
			if ((bindings.dirtySamplers & (1u << index)) == 0 || !bindings.samplers[index])
			{
				continue;
			}

			if (vertex)
			{
				[m_renderEncoder setVertexSamplerState:bindings.samplers[index] atIndex:index];
			}
			else
			{
				[m_renderEncoder setFragmentSamplerState:bindings.samplers[index] atIndex:index];
			}
		}
		bindings.dirtySamplers = 0;
	}

	void GraphicsDeviceMetal::BindPlaceholders(const std::vector<ShaderTextureUse>& textures, const uint32* usedBuffers, const uint32* usedSamplers)
	{
		for (uint32 stageIndex = 0; stageIndex < 2; ++stageIndex)
		{
			StageBindings& bindings = m_stages[stageIndex];
			const bool vertex = stageIndex == 0;

			for (uint32 index = 0; index < MaxBufferSlots; ++index)
			{
				if ((usedBuffers[stageIndex] & (1u << index)) == 0)
				{
					continue;
				}

				const bool bound = index < metal_bindings::MaxConstantBuffers ? bindings.constantBuffers[index] != nullptr : bindings.buffers[index] != nullptr;
				if (bound)
				{
					continue;
				}

				if (vertex)
				{
					[m_renderEncoder setVertexBuffer:m_zeroBuffer offset:0 atIndex:index];
				}
				else
				{
					[m_renderEncoder setFragmentBuffer:m_zeroBuffer offset:0 atIndex:index];
				}

				// Bind the real buffer again as soon as there is one.
				if (index < metal_bindings::MaxConstantBuffers)
				{
					bindings.dirtyConstantBuffers |= 1u << index;
				}
				else
				{
					bindings.dirtyBuffers |= 1u << index;
				}
			}

			for (uint32 index = 0; index < metal_bindings::MaxSamplers; ++index)
			{
				if ((usedSamplers[stageIndex] & (1u << index)) != 0 && !bindings.samplers[index])
				{
					if (vertex)
					{
						[m_renderEncoder setVertexSamplerState:m_defaultSampler atIndex:index];
					}
					else
					{
						[m_renderEncoder setFragmentSamplerState:m_defaultSampler atIndex:index];
					}
					bindings.dirtySamplers |= 1u << index;
				}
			}
		}

		for (const ShaderTextureUse& use : textures)
		{
			StageBindings& bindings = m_stages[use.stage];
			id<MTLTexture> bound = use.index < MaxTextureSlots ? bindings.textures[use.index] : nil;

			// A texture of the wrong kind is as bad as none (e.g. a 2D texture where the shader
			// samples a depth texture), so replace those as well.
			const bool matches = bound && bound.textureType == use.type && (!use.depth || bound.pixelFormat == MTLPixelFormatDepth32Float || bound.pixelFormat == MTLPixelFormatDepth32Float_Stencil8);
			if (matches)
			{
				continue;
			}

			id<MTLTexture> placeholder = GetPlaceholderTexture(use);
			if (use.stage == 0)
			{
				[m_renderEncoder setVertexTexture:placeholder atIndex:use.index];
			}
			else
			{
				[m_renderEncoder setFragmentTexture:placeholder atIndex:use.index];
			}

			if (use.index < MaxTextureSlots)
			{
				bindings.dirtyTextures |= 1u << use.index;
			}
		}
	}

	void GraphicsDeviceMetal::BindVertexStreams()
	{
		for (uint32 stream = 0; stream < metal_bindings::MaxVertexStreams; ++stream)
		{
			if (VertexBufferMetal* buffer = m_vertexStreams[stream])
			{
				[m_renderEncoder setVertexBuffer:buffer->GetBuffer().Use() offset:0 atIndex:metal_bindings::VertexBufferTop - stream];
			}
		}

		[m_renderEncoder setVertexBuffer:m_nullVertexBuffer offset:0 atIndex:NullVertexBufferIndex];
	}

	NSUInteger GraphicsDeviceMetal::AllocateTransient(const void* data, const NSUInteger size, __strong id<MTLBuffer>& outBuffer)
	{
		const NSUInteger alignedSize = (size + 255) & ~static_cast<NSUInteger>(255);
		const uint64 currentFrame = m_frameIndex;
		const uint64 completedFrame = GetCompletedFrame();

		auto fits = [&](TransientChunk& chunk)
		{
			return chunk.buffer.length - chunk.used >= alignedSize;
		};

		TransientChunk* target = nullptr;
		if (m_currentTransientChunk < m_transientChunks.size())
		{
			TransientChunk& current = m_transientChunks[m_currentTransientChunk];
			if (current.frame == currentFrame && fits(current))
			{
				target = &current;
			}
		}

		if (!target)
		{
			for (size_t i = 0; i < m_transientChunks.size(); ++i)
			{
				TransientChunk& chunk = m_transientChunks[i];
				if (chunk.frame <= completedFrame && chunk.buffer.length >= alignedSize)
				{
					chunk.used = 0;
					chunk.frame = currentFrame;
					m_currentTransientChunk = i;
					target = &chunk;
					break;
				}
			}
		}

		if (!target)
		{
			TransientChunk chunk;
			chunk.buffer = [m_device newBufferWithLength:std::max(TransientChunkSize, alignedSize) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
			chunk.used = 0;
			chunk.frame = currentFrame;
			m_transientChunks.push_back(chunk);
			m_currentTransientChunk = m_transientChunks.size() - 1;
			target = &m_transientChunks.back();
		}

		const NSUInteger offset = target->used;
		std::memcpy(static_cast<uint8*>([target->buffer contents]) + offset, data, size);
		target->used += alignedSize;
		outBuffer = target->buffer;
		return offset;
	}

	bool GraphicsDeviceMetal::PrepareDraw()
	{
		UpdateMatrixBuffers();

		if (!m_vertexShader)
		{
			return false;
		}

		if (!EnsureRenderEncoder())
		{
			return false;
		}

		const PipelineEntry* pipeline = GetPipelineState();
		if (!pipeline)
		{
			return false;
		}

		if (pipeline->state != m_currentPipeline)
		{
			[m_renderEncoder setRenderPipelineState:pipeline->state];
			m_currentPipeline = pipeline->state;
		}
		m_currentPipelineEntry = pipeline;

		id<MTLDepthStencilState> depthStencil = GetDepthStencilState();
		if (depthStencil != m_currentDepthStencil)
		{
			[m_renderEncoder setDepthStencilState:depthStencil];
			m_currentDepthStencil = depthStencil;
		}

		NSUInteger targetWidth = 0;
		NSUInteger targetHeight = 0;
		if (m_colorTargetCount > 0 && m_colorTargets[0])
		{
			targetWidth = m_colorTargets[0].width;
			targetHeight = m_colorTargets[0].height;
		}
		else if (m_depthTarget)
		{
			targetWidth = m_depthTarget.width;
			targetHeight = m_depthTarget.height;
		}

		if (m_viewportDirty)
		{
			MTLViewport viewport;
			viewport.originX = m_viewX;
			viewport.originY = m_viewY;
			viewport.width = std::max(1, m_viewW);
			viewport.height = std::max(1, m_viewH);
			viewport.znear = m_viewMinZ;
			viewport.zfar = m_viewMaxZ;
			[m_renderEncoder setViewport:viewport];
			m_viewportDirty = false;
		}

		if (m_rasterStateDirty)
		{
			switch (m_cullMode)
			{
			case FaceCullMode::Back:
				[m_renderEncoder setCullMode:MTLCullModeBack];
				break;
			case FaceCullMode::Front:
				[m_renderEncoder setCullMode:MTLCullModeFront];
				break;
			default:
				[m_renderEncoder setCullMode:MTLCullModeNone];
				break;
			}

			[m_renderEncoder setFrontFacingWinding:MTLWindingClockwise];
			[m_renderEncoder setTriangleFillMode:m_fillMode == FillMode::Wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
			[m_renderEncoder setDepthBias:m_depthBias slopeScale:m_slopeScaledDepthBias clamp:m_depthBiasClamp];
			[m_renderEncoder setStencilReferenceValue:m_stencilRef];

			// Metal rejects scissor rects that leave the render target.
			MTLScissorRect scissor { 0, 0, targetWidth, targetHeight };
			if (m_scissorEnabled)
			{
				scissor.x = std::min(m_scissorRect.x, targetWidth);
				scissor.y = std::min(m_scissorRect.y, targetHeight);
				scissor.width = std::min(m_scissorRect.width, targetWidth - scissor.x);
				scissor.height = std::min(m_scissorRect.height, targetHeight - scissor.y);
			}
			[m_renderEncoder setScissorRect:scissor];

			m_rasterStateDirty = false;
		}

		if (m_scissorEnabled && (m_scissorRect.width == 0 || m_scissorRect.height == 0))
		{
			// Nothing of the draw can be visible; Metal does not accept an empty scissor rect.
			return false;
		}

		ApplyStageBindings(ShaderType::VertexShader);
		ApplyStageBindings(ShaderType::PixelShader);
		BindPlaceholders(pipeline->textures, pipeline->usedBuffers, pipeline->usedSamplers);
		BindVertexStreams();

		return true;
	}

	// ----------------------------------------------------------------------------------------
	// Draws

	void GraphicsDeviceMetal::DrawPrimitives(const uint32 vertexCount, const uint32 start)
	{
		[m_renderEncoder drawPrimitives:ToMetalPrimitive(m_topologyType) vertexStart:start vertexCount:vertexCount];
	}

	void GraphicsDeviceMetal::Draw(const uint32 vertexCount, const uint32 start)
	{
		if (vertexCount == 0 || !PrepareDraw())
		{
			return;
		}

		DrawPrimitives(vertexCount, start);

		++m_batchCount;
		++m_totalDrawCount;
		m_totalPrimitiveCount += vertexCount / 3;
	}

	void GraphicsDeviceMetal::DrawIndexed(const uint32 startIndex, const uint32 endIndex)
	{
		if (!m_indexBuffer)
		{
			return;
		}

		const uint32 indexCount = endIndex == 0 ? m_indexCount - startIndex : endIndex - startIndex;
		if (indexCount == 0 || !PrepareDraw())
		{
			return;
		}

		[m_renderEncoder drawIndexedPrimitives:ToMetalPrimitive(m_topologyType)
			indexCount:indexCount
			indexType:m_indexBuffer->GetMetalIndexType()
			indexBuffer:m_indexBuffer->GetBuffer().Use()
			indexBufferOffset:startIndex * m_indexBuffer->GetIndexByteSize()];

		++m_batchCount;
		++m_totalDrawCount;
		m_totalPrimitiveCount += indexCount / 3;
	}

	void GraphicsDeviceMetal::DrawIndexedInstanced(const uint32 indexCount, const uint32 instanceCount, const uint32 startIndex, const int32 baseVertex, const uint32 startInstance)
	{
		if (!m_indexBuffer || indexCount == 0 || instanceCount == 0 || !PrepareDraw())
		{
			return;
		}

		[m_renderEncoder drawIndexedPrimitives:ToMetalPrimitive(m_topologyType)
			indexCount:indexCount
			indexType:m_indexBuffer->GetMetalIndexType()
			indexBuffer:m_indexBuffer->GetBuffer().Use()
			indexBufferOffset:startIndex * m_indexBuffer->GetIndexByteSize()
			instanceCount:instanceCount
			baseVertex:baseVertex
			baseInstance:startInstance];

		++m_batchCount;
		++m_totalDrawCount;
		m_totalPrimitiveCount += static_cast<uint64>(indexCount / 3) * instanceCount;
	}

	void GraphicsDeviceMetal::Render(const RenderOperation& operation)
	{
		PROFILE_SCOPE("Metal::Render");

		GraphicsDevice::Render(operation);

		if (!operation.material || !operation.vertexData)
		{
			return;
		}

		MaterialInterface* materialPtr = operation.material.get();
		const bool materialChanged = materialPtr != m_lastBoundMaterial || operation.pixelShaderType != m_lastBoundPixelShaderType;
		if (materialChanged)
		{
			operation.material->Apply(*this, MaterialDomain::Surface, operation.pixelShaderType);
			m_lastBoundMaterial = materialPtr;
			m_lastBoundPixelShaderType = operation.pixelShaderType;
		}

		const bool isInstanced = operation.instanceBuffer != nullptr && operation.instanceCount > 0;
		VertexShaderType vsType = VertexShaderType::Default;
		VertexDeclaration* vertexDeclaration = operation.vertexData->vertexDeclaration;
		if (isInstanced)
		{
			vsType = VertexShaderType::Instanced;
		}
		else
		{
			auto cacheIt = m_blendIndicesCache.find(vertexDeclaration);
			bool hasVertexAnimData;
			if (cacheIt != m_blendIndicesCache.end())
			{
				hasVertexAnimData = cacheIt->second;
			}
			else
			{
				hasVertexAnimData = vertexDeclaration->FindElementBySemantic(VertexElementSemantic::BlendIndices) != nullptr;
				m_blendIndicesCache[vertexDeclaration] = hasVertexAnimData;
			}
			vsType = hasVertexAnimData ? VertexShaderType::SkinnedHigh : VertexShaderType::Default;
		}

		ShaderBase* vertexShader = operation.material->GetVertexShader(vsType).get();
		if (!vertexShader && vsType != VertexShaderType::Default)
		{
			if (vsType == VertexShaderType::Instanced)
			{
				static std::set<std::string> s_warnedInstancedMaterials;
				const std::string materialName(operation.material->GetName());
				if (s_warnedInstancedMaterials.insert(materialName).second)
				{
					WLOG("Material '" << materialName << "' has no instanced vertex shader variant - instanced draw skipped.");
				}
				return;
			}

			vertexShader = operation.material->GetVertexShader(VertexShaderType::Default).get();
		}

		if (!vertexShader)
		{
			return;
		}

		vertexShader->Set();

		uint16 instanceSlot = 0;
		for (const auto& [slot, vertexBuffer] : operation.vertexData->vertexBufferBinding->GetBindings())
		{
			if (!vertexBuffer)
			{
				continue;
			}

			vertexBuffer->Set(slot);
			instanceSlot = std::max<uint16>(instanceSlot, slot + 1);
		}

		uint32 vsSlot = 1;
		for (ConstantBuffer* buffer : operation.vertexConstantBuffers)
		{
			BindConstantBuffer(ShaderType::VertexShader, vsSlot++, static_cast<ConstantBufferMetal*>(buffer));
		}

		uint32 psSlot = 1;
		for (ConstantBuffer* buffer : operation.pixelConstantBuffers)
		{
			BindConstantBuffer(ShaderType::PixelShader, psSlot++, static_cast<ConstantBufferMetal*>(buffer));
		}

		if (const ConstantBufferPtr globalParamBuffer = GlobalShaderParameters::Get().GetBuffer(*this))
		{
			globalParamBuffer->BindToStage(ShaderType::PixelShader, kGlobalShaderParametersPsSlot);
		}

		// Unlike D3D11 the parameter buffers are bound for every draw: other draws may have bound
		// different buffers to these slots in between, and binding only stores a pointer here.
		if (const ConstantBufferPtr scalarBuffer = operation.material->GetParameterBuffer(MaterialParameterType::Scalar, *this))
		{
			scalarBuffer->BindToStage(ShaderType::PixelShader, psSlot++);
		}
		if (const ConstantBufferPtr vectorBuffer = operation.material->GetParameterBuffer(MaterialParameterType::Vector, *this))
		{
			vectorBuffer->BindToStage(ShaderType::PixelShader, psSlot++);
		}

		SetFaceCullMode(GetMaterialCullMode(operation.material->IsTwoSided(), operation.mirrored));

		if (isInstanced)
		{
			operation.instanceBuffer->Set(instanceSlot);
			SetVertexLayoutFromDeclaration(*vertexDeclaration, true, instanceSlot);
			SetTopologyType(operation.topology);

			if (operation.indexData)
			{
				operation.indexData->indexBuffer->Set(0);
				DrawIndexedInstanced(static_cast<uint32>(operation.indexData->indexCount), operation.instanceCount, static_cast<uint32>(operation.indexData->indexStart), 0, 0);
			}
		}
		else
		{
			SetVertexLayoutFromDeclaration(*vertexDeclaration, false, 0);
			SetTopologyType(operation.topology);

			if (operation.indexData)
			{
				operation.indexData->indexBuffer->Set(0);
				DrawIndexed(static_cast<uint32>(operation.indexData->indexStart), static_cast<uint32>(operation.indexData->indexStart + operation.indexData->indexCount));
			}
			else
			{
				Draw(static_cast<uint32>(operation.vertexData->vertexCount), static_cast<uint32>(operation.vertexData->vertexStart));
			}
		}
	}

	// ----------------------------------------------------------------------------------------
	// Compute

	const GraphicsDeviceMetal::ComputePipelineEntry* GraphicsDeviceMetal::GetComputePipelineState(ShaderMetal& shader)
	{
		const void* key = (__bridge const void*)shader.GetFunction();
		if (const auto it = m_computePipelineStates.find(key); it != m_computePipelineStates.end())
		{
			return it->second.state ? &it->second : nullptr;
		}

		NSError* error = nil;
		MTLAutoreleasedComputePipelineReflection reflection = nil;
		id<MTLComputePipelineState> state = [m_device newComputePipelineStateWithFunction:shader.GetFunction() options:MTLPipelineOptionBindingInfo reflection:&reflection error:&error];

		ComputePipelineEntry& entry = m_computePipelineStates[key];
		if (!state)
		{
			ELOG("Failed to create Metal compute pipeline: " << (error ? [[error localizedDescription] UTF8String] : "unknown error"));
			return nullptr;
		}

		entry.state = state;
		CollectBindings(reflection.bindings, 2, entry.usedBuffers, entry.usedSamplers, entry.textures);
		return &entry;
	}

	void GraphicsDeviceMetal::ApplyComputeBindings(const ComputePipelineEntry& entry)
	{
		StageBindings& bindings = m_stages[2];

		for (uint32 slot = 0; slot < metal_bindings::MaxConstantBuffers; ++slot)
		{
			ConstantBufferMetal* buffer = bindings.constantBuffers[slot];
			if (buffer)
			{
				if (buffer->GetSize() <= MaxSetBytesSize)
				{
					[m_computeEncoder setBytes:buffer->GetData() length:buffer->GetSize() atIndex:slot];
				}
				else
				{
					id<MTLBuffer> transient = nil;
					const NSUInteger offset = AllocateTransient(buffer->GetData(), buffer->GetSize(), transient);
					[m_computeEncoder setBuffer:transient offset:offset atIndex:slot];
				}
			}
			else if (entry.usedBuffers & (1u << slot))
			{
				[m_computeEncoder setBuffer:m_zeroBuffer offset:0 atIndex:slot];
			}
		}

		for (uint32 index = metal_bindings::MaxConstantBuffers; index < MaxBufferSlots; ++index)
		{
			if (bindings.buffers[index])
			{
				[m_computeEncoder setBuffer:bindings.buffers[index]->Use() offset:0 atIndex:index];
			}
			else if (entry.usedBuffers & (1u << index))
			{
				[m_computeEncoder setBuffer:m_zeroBuffer offset:0 atIndex:index];
			}
		}

		for (uint32 index = 0; index < metal_bindings::MaxSamplers; ++index)
		{
			if (bindings.samplers[index])
			{
				[m_computeEncoder setSamplerState:bindings.samplers[index] atIndex:index];
			}
			else if (entry.usedSamplers & (1u << index))
			{
				[m_computeEncoder setSamplerState:m_defaultSampler atIndex:index];
			}
		}

		for (const ShaderTextureUse& use : entry.textures)
		{
			id<MTLTexture> bound = use.index < MaxTextureSlots ? bindings.textures[use.index] : nil;
			if (bound && bound.textureType == use.type)
			{
				[m_computeEncoder setTexture:bound atIndex:use.index];
			}
			else
			{
				[m_computeEncoder setTexture:GetPlaceholderTexture(use) atIndex:use.index];
			}
		}
	}

	void GraphicsDeviceMetal::Dispatch(const uint32 groupsX, const uint32 groupsY, const uint32 groupsZ)
	{
		if (!m_computeShader || groupsX == 0 || groupsY == 0 || groupsZ == 0)
		{
			return;
		}

		const ComputePipelineEntry* entry = GetComputePipelineState(*m_computeShader);
		if (!entry)
		{
			return;
		}

		if (!m_computeEncoder)
		{
			// Running compute work in between draws ends the render pass; the next draw continues
			// it with a load action, as the attachments keep their contents.
			EndRenderEncoder();
			if (m_blitEncoder)
			{
				[m_blitEncoder endEncoding];
				m_blitEncoder = nil;
			}
			m_computeEncoder = [GetCommandBuffer() computeCommandEncoder];
		}

		[m_computeEncoder setComputePipelineState:entry->state];
		ApplyComputeBindings(*entry);
		[m_computeEncoder dispatchThreadgroups:MTLSizeMake(groupsX, groupsY, groupsZ) threadsPerThreadgroup:m_computeShader->GetThreadgroupSize()];
	}

	void GraphicsDeviceMetal::ClearComputeBindings()
	{
		m_stages[2].Clear();
		m_computeShader = nullptr;
	}

	// ----------------------------------------------------------------------------------------
	// System information

	void GraphicsDeviceMetal::SetHardwareCursor(void* osCursorData)
	{
		m_hardwareCursor = osCursorData;
	}

	void* GraphicsDeviceMetal::GetHardwareCursor()
	{
		return m_hardwareCursor;
	}

	std::string GraphicsDeviceMetal::GetPrimaryMonitorResolution() const
	{
		const NSRect frame = [[NSScreen mainScreen] frame];
		return std::to_string(static_cast<int>(frame.size.width)) + "x" + std::to_string(static_cast<int>(frame.size.height));
	}

	bool GraphicsDeviceMetal::ValidateFullscreenResolution(const uint16 width, const uint16 height) const
	{
		const NSRect frame = [[NSScreen mainScreen] frame];
		return width <= frame.size.width && height <= frame.size.height;
	}

	std::vector<std::pair<uint16, uint16>> GraphicsDeviceMetal::GetSupportedResolutions() const
	{
		return GraphicsDevice::GetSupportedResolutions();
	}

	std::vector<DisplayMonitor> GraphicsDeviceMetal::GetDisplayMonitors() const
	{
		std::vector<DisplayMonitor> result;
		NSScreen* mainScreen = [NSScreen mainScreen];
		for (NSScreen* screen in [NSScreen screens])
		{
			DisplayMonitor monitor;
			monitor.name = [[screen localizedName] UTF8String];
			monitor.x = static_cast<int32>(screen.frame.origin.x);
			monitor.y = static_cast<int32>(screen.frame.origin.y);
			monitor.width = static_cast<uint16>(screen.frame.size.width);
			monitor.height = static_cast<uint16>(screen.frame.size.height);
			monitor.primary = screen == mainScreen;
			if (monitor.primary)
			{
				result.insert(result.begin(), monitor);
			}
			else
			{
				result.push_back(monitor);
			}
		}

		if (result.empty())
		{
			return GraphicsDevice::GetDisplayMonitors();
		}

		return result;
	}

	std::string GraphicsDeviceMetal::GetAdapterDescription() const
	{
		return m_device ? [[m_device name] UTF8String] : std::string();
	}

	GpuInfo GraphicsDeviceMetal::GetGpuInfo() const
	{
		GpuInfo info;
		if (m_device)
		{
			info.name = [[m_device name] UTF8String];
			info.integrated = [m_device hasUnifiedMemory];
			info.dedicatedVideoMemory = [m_device recommendedMaxWorkingSetSize];
		}

		return info;
	}
}
