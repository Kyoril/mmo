// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/graphics_device.h"
#include "base/typedefs.h"
#include "msl_cross/metal_shader_blob.h"

#include <array>
#include <atomic>
#include <list>
#include <unordered_map>
#include <vector>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace mmo
{
	class ShaderMetal;
	class ConstantBufferMetal;
	class VertexBufferMetal;
	class IndexBufferMetal;
	class RenderWindowMetal;

	/// @brief Converts an engine pixel format to the Metal format used for render targets of it.
	MTLPixelFormat ToMetalRenderTargetFormat(PixelFormat format);

	/// @brief Implements the graphics device on top of Metal.
	/// @remarks The engine drives its devices like a D3D11 immediate context: state is set piece by
	///          piece and draws happen whenever. This class records that state and turns it into
	///          Metal objects lazily: a render command encoder is opened on the first draw or clear
	///          after the render targets changed, pipeline and depth states are created on demand
	///          and cached, and constant buffers are snapshotted into the command stream per draw.
	class GraphicsDeviceMetal final
		: public GraphicsDevice
	{
	public:
		/// Number of frames the CPU may record ahead of the GPU.
		static constexpr uint32 MaxFramesInFlight = 3;

		/// Number of texture slots per stage the engine uses (t0 to t31).
		static constexpr uint32 MaxTextureSlots = 32;

		/// Number of Metal buffer slots per stage.
		static constexpr uint32 MaxBufferSlots = 31;

		/// Number of simultaneously bound render targets.
		static constexpr uint32 MaxColorTargets = 8;

	public:
		GraphicsDeviceMetal();

		~GraphicsDeviceMetal() override;

	public:
		// ~ Begin GraphicsDevice
		void SetHardwareCursor(void* osCursorData) override;

		void* GetHardwareCursor() override;

		Matrix4 MakeProjectionMatrix(const Radian& fovY, float aspect, float nearPlane, float farPlane) override;

		Matrix4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearPlane, float farPlane) override;

		void Reset() override;

		void SetClearColor(uint32 clearColor) override;

		void Create(const GraphicsDeviceDesc& desc) override;

		void Clear(ClearFlags flags = ClearFlags::None) override;

		VertexBufferPtr CreateVertexBuffer(size_t vertexCount, size_t vertexSize, BufferUsage usage, const void* initialData = nullptr) override;

		IndexBufferPtr CreateIndexBuffer(size_t indexCount, IndexBufferSize indexSize, BufferUsage usage, const void* initialData = nullptr) override;

		ConstantBufferPtr CreateConstantBuffer(size_t size, const void* initialData = nullptr) override;

		StructuredBufferPtr CreateStructuredBuffer(size_t elementSize, size_t elementCount, const void* initialData = nullptr) override;

		ShaderPtr CreateShader(ShaderType type, const void* shaderCode, size_t shaderCodeSize) override;

		[[nodiscard]] bool SupportsNullPixelShaderForShadows() const override { return true; }

		void BindNullPixelShader() override;

		void SetDepthBias(float bias) override;

		void SetSlopeScaledDepthBias(float bias) override;

		void SetDepthBiasClamp(float bias) override;

		void Render(const RenderOperation& operation) override;

		OcclusionQueryPtr CreateOcclusionQuery() override;

		VolumeTexturePtr CreateVolumeTexture(uint16 width, uint16 height, uint16 depth, VolumeFormat format, bool writable) override;

		TextureArrayPtr CreateTextureArray(uint16 width, uint16 height, uint16 layers, TextureArrayFormat format, bool writable, bool mipmapped) override;

		SamplerStatePtr CreateSamplerState(const SamplerDesc& desc) override;

		void Dispatch(uint32 groupsX, uint32 groupsY, uint32 groupsZ) override;

		void ClearComputeBindings() override;

		void Draw(uint32 vertexCount, uint32 start = 0) override;

		void DrawIndexed(uint32 startIndex = 0, uint32 endIndex = 0) override;

		void DrawIndexedInstanced(uint32 indexCount, uint32 instanceCount, uint32 startIndex = 0, int32 baseVertex = 0, uint32 startInstance = 0) override;

		void SetTopologyType(TopologyType type) override;

		void SetVertexFormat(VertexFormat format) override;

		void SetBlendMode(BlendMode blendMode) override;

		void CaptureState() override;

		void RestoreState() override;

		void SetTransformMatrix(TransformType type, Matrix4 const& matrix) override;

		TexturePtr CreateTexture(uint16 width = 0, uint16 height = 0, BufferUsage usage = BufferUsage::Static) override;

		void BindTexture(TexturePtr texture, ShaderType shader, uint32 slot) override;

		void SetViewport(int32 x, int32 y, int32 w, int32 h, float minZ, float maxZ) override;

		void SetClipRect(int32 x, int32 y, int32 w, int32 h) override;

		void ResetClipRect() override;

		RenderWindowPtr CreateRenderWindow(std::string name, uint16 width, uint16 height, bool fullScreen) override;

		RenderTexturePtr CreateRenderTexture(std::string name, uint16 width, uint16 height, RenderTextureFlags flags, PixelFormat colorFormat = PixelFormat::R8G8B8A8, PixelFormat depthFormat = PixelFormat::D32F) override;

		void SetRenderTargets(RenderTexturePtr* renderTargets, uint32 count) override;

		void SetRenderTargetsWithDepthStencil(RenderTexturePtr* renderTargets, uint32 count, RenderTexturePtr depthStencilRT) override;

		void SetFillMode(FillMode mode) override;

		void SetFaceCullMode(FaceCullMode mode) override;

		void SetTextureAddressMode(TextureAddressMode modeU, TextureAddressMode modeV, TextureAddressMode modeW) override;

		void SetTextureFilter(TextureFilter filter) override;

		void SetDepthEnabled(bool enable) override;

		void SetDepthWriteEnabled(bool enable) override;

		void SetDepthTestComparison(DepthTestMethod comparison) override;

		void SetStencilWriteEnabled(bool enable) override;

		void SetStencilRef(uint8 reference) override;

		std::unique_ptr<MaterialCompiler> CreateMaterialCompiler() override;

		std::unique_ptr<ShaderCompiler> CreateShaderCompiler() override;

		void FlushCommands() override;

		uint64 GetBatchCount() const override { return m_lastFrameBatchCount; }

		std::string GetPrimaryMonitorResolution() const override;

		bool ValidateFullscreenResolution(uint16 width, uint16 height) const override;

		std::vector<std::pair<uint16, uint16>> GetSupportedResolutions() const override;

		std::vector<DisplayMonitor> GetDisplayMonitors() const override;

		std::string GetAdapterDescription() const override;

		GpuInfo GetGpuInfo() const override;

		void SetVSyncEnabled(bool enable) override;

		void SetMaxAnisotropy(uint32 maxAnisotropy) override;

		void SetTextureFilterCap(TextureFilter maxFilter) override;

		void InvalidateBoundMaterial() override { m_lastBoundMaterial = nullptr; }

		[[nodiscard]] uint64 GetTotalDrawCount() const override { return m_totalDrawCount; }

		[[nodiscard]] uint64 GetTotalPrimitiveCount() const override { return m_totalPrimitiveCount; }
		// ~ End GraphicsDevice

	public:
		[[nodiscard]] id<MTLDevice> GetDevice() const { return m_device; }

		[[nodiscard]] id<MTLCommandQueue> GetCommandQueue() const { return m_commandQueue; }

		[[nodiscard]] bool IsVSyncEnabled() const { return m_vsync; }

		/// Index of the frame currently being recorded. Increases with every committed frame.
		[[nodiscard]] uint64 GetCurrentFrame() const { return m_frameIndex; }

		/// Index of the newest frame the GPU has finished. Resources last used in this frame or
		/// earlier may be overwritten by the CPU.
		[[nodiscard]] uint64 GetCompletedFrame() const { return m_completedFrame.load(std::memory_order_acquire); }

		/// @brief Returns a blit encoder for uploads. Its command buffer is committed before the next
		///        frame's command buffer, so everything recorded here is visible to the frame.
		id<MTLBlitCommandEncoder> GetUploadEncoder();

		/// @brief Commits pending uploads and waits for them, for code that reads data back.
		void FlushUploadsAndWait();

		/// @brief Runs a blit encoder in the frame's command buffer, in order with its rendering.
		id<MTLBlitCommandEncoder> BeginFrameBlit();

		/// @brief Commits all recorded work and blocks until the GPU has finished it.
		void FinishAndWait();

		/// @brief Binds render targets. Pass nil for unused color slots.
		void SetRenderTargetTextures(const id<MTLTexture>* colors, uint32 colorCount, id<MTLTexture> depth);

		/// @brief Clears a texture to the engine's clear values. Bound targets are cleared when their
		///        next render pass starts, anything else right away.
		void ClearRenderTexture(id<MTLTexture> color, id<MTLTexture> depth, ClearFlags flags);

		/// @brief Presents a drawable and finishes the frame.
		void PresentFrame(id<CAMetalDrawable> drawable);

		/// @brief Forgets a render target texture that is about to be released.
		void ForgetTexture(id<MTLTexture> texture);

		void SetShader(ShaderMetal* shader);

		void ForgetShader(const ShaderMetal* shader);

		void SetVertexBuffer(uint16 slot, VertexBufferMetal* buffer);

		void ForgetVertexBuffer(const VertexBufferMetal* buffer);

		void SetIndexBuffer(IndexBufferMetal* buffer);

		void ForgetIndexBuffer(const IndexBufferMetal* buffer);

		void BindConstantBuffer(ShaderType stage, uint32 slot, ConstantBufferMetal* buffer);

		void ForgetConstantBuffer(const ConstantBufferMetal* buffer);

		/// @brief Binds a raw Metal buffer at a buffer index of a stage (structured buffers).
		void BindStageBuffer(ShaderType stage, uint32 index, id<MTLBuffer> buffer, NSUInteger offset);

		/// @brief Binds a texture to a texture index of a stage. Applies the current sampler to the
		///        sampler slot of the same number when applySampler is set, like D3D11 BindTexture.
		void BindStageTexture(ShaderType stage, uint32 index, id<MTLTexture> texture);

		void BindStageSampler(ShaderType stage, uint32 index, id<MTLSamplerState> sampler);

		/// @brief Returns the sampler for the device's current filter and address mode.
		id<MTLSamplerState> GetCurrentSampler();

		id<MTLSamplerState> GetSampler(MTLSamplerDescriptor* descriptor);

		/// @brief Records that a buffer was used by the frame being recorded.
		void LatchFrameBatchCount();

		RenderWindowMetal* GetMainWindow() const { return m_mainWindow; }

	private:
		struct VertexLayoutElement
		{
			VertexElementSemantic semantic;
			uint16 index;
			MTLVertexFormat format;
			uint16 stream;
			uint32 offset;
		};

		/// The vertex layout of the next draws: either one of the fixed vertex formats or a vertex
		/// declaration, optionally with an instance stream.
		struct VertexLayout
		{
			std::vector<VertexLayoutElement> elements;
			bool instanced = false;
			uint16 instanceStream = 0;
			uint64 hash = 0;
		};

		struct StageBindings
		{
			id<MTLTexture> textures[MaxTextureSlots] = {};
			id<MTLSamplerState> samplers[metal_bindings::MaxSamplers] = {};
			ConstantBufferMetal* constantBuffers[metal_bindings::MaxConstantBuffers] = {};
			uint64 constantBufferVersions[metal_bindings::MaxConstantBuffers] = {};
			id<MTLBuffer> buffers[MaxBufferSlots] = {};
			NSUInteger bufferOffsets[MaxBufferSlots] = {};
			uint32 dirtyTextures = 0;
			uint32 dirtySamplers = 0;
			uint32 dirtyConstantBuffers = 0;
			uint32 dirtyBuffers = 0;

			void MarkAllDirty()
			{
				dirtyTextures = 0xFFFFFFFF;
				dirtySamplers = 0xFFFFFFFF;
				dirtyConstantBuffers = 0xFFFFFFFF;
				dirtyBuffers = 0xFFFFFFFF;
			}

			void Clear()
			{
				*this = StageBindings{};
				MarkAllDirty();
			}
		};

		/// A resource a pipeline's shaders use, from the pipeline reflection. Used to bind
		/// placeholders where the engine left a slot empty, which D3D11 tolerates but Metal does not.
		struct ShaderTextureUse
		{
			uint8 stage;
			uint8 index;
			MTLTextureType type;
			MTLDataType dataType;
			bool depth;
		};

		struct PipelineEntry
		{
			id<MTLRenderPipelineState> state = nil;
			uint32 usedBuffers[2] = {};
			uint32 usedSamplers[2] = {};
			std::vector<ShaderTextureUse> textures;
		};

		struct ComputePipelineEntry
		{
			id<MTLComputePipelineState> state = nil;
			uint32 usedBuffers = 0;
			uint32 usedSamplers = 0;
			std::vector<ShaderTextureUse> textures;
		};

		struct PendingClear
		{
			bool color[MaxColorTargets] = {};
			bool depth = false;
			bool stencil = false;
		};

		/// Uploads small transient data (constant buffer snapshots too large for setBytes).
		struct TransientChunk
		{
			id<MTLBuffer> buffer;
			NSUInteger used = 0;
			uint64 frame = 0;
		};

	private:
		void CreateBuiltInShaders();

		void CreateMatrixBuffers();

		id<MTLCommandBuffer> GetCommandBuffer();

		void CommitUploads();

		/// Ends the open render or compute encoder, executing pending clears first.
		void EndEncoders();

		void EndRenderEncoder();

		void EndComputeEncoder();

		/// Opens a render encoder on the current targets if none is open.
		bool EnsureRenderEncoder();

		/// Runs a render pass that only clears, for clears of targets that are not bound.
		void ExecuteStandaloneClear(id<MTLTexture> color, id<MTLTexture> depth, ClearFlags flags);

		bool PrepareDraw();

		const PipelineEntry* GetPipelineState();

		const ComputePipelineEntry* GetComputePipelineState(ShaderMetal& shader);

		void BindPlaceholders(const std::vector<ShaderTextureUse>& textures, const uint32* usedBuffers, const uint32* usedSamplers);

		id<MTLTexture> GetPlaceholderTexture(const ShaderTextureUse& use);

		static void CollectBindings(NSArray<id<MTLBinding>>* bindings, uint8 stage, uint32& usedBuffers, uint32& usedSamplers, std::vector<ShaderTextureUse>& textures);

		MTLVertexDescriptor* BuildVertexDescriptor(id<MTLFunction> vertexFunction);

		id<MTLDepthStencilState> GetDepthStencilState();

		void ApplyStageBindings(ShaderType stage);

		void BindVertexStreams();

		void ApplyComputeBindings(const ComputePipelineEntry& entry);

		void UpdateMatrixBuffers();

		void ApplyEffectiveTextureFilter();

		NSUInteger AllocateTransient(const void* data, NSUInteger size, __strong id<MTLBuffer>& outBuffer);

		void SetVertexLayoutFromDeclaration(VertexDeclaration& declaration, bool instanced, uint16 instanceStream);

		void SetVertexLayoutFromFormat(VertexFormat format);

		StageBindings& GetStage(ShaderType stage);

		void DrawPrimitives(uint32 vertexCount, uint32 start);

	private:
		id<MTLDevice> m_device = nil;
		id<MTLCommandQueue> m_commandQueue = nil;
		id<MTLCommandBuffer> m_commandBuffer = nil;
		id<MTLRenderCommandEncoder> m_renderEncoder = nil;
		id<MTLComputeCommandEncoder> m_computeEncoder = nil;
		id<MTLBlitCommandEncoder> m_blitEncoder = nil;
		id<MTLCommandBuffer> m_uploadCommandBuffer = nil;
		id<MTLBlitCommandEncoder> m_uploadEncoder = nil;
		dispatch_semaphore_t m_frameSemaphore = nullptr;

		uint64 m_frameIndex = 1;
		std::atomic<uint64> m_completedFrame { 0 };

		RenderWindowMetal* m_mainWindow = nullptr;

		// Render targets
		id<MTLTexture> m_colorTargets[MaxColorTargets] = {};
		uint32 m_colorTargetCount = 0;
		id<MTLTexture> m_depthTarget = nil;
		PendingClear m_pendingClear;
		float m_clearColorFloat[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		// Shader state
		ShaderMetal* m_vertexShader = nullptr;
		ShaderMetal* m_pixelShader = nullptr;
		ShaderMetal* m_computeShader = nullptr;
		StageBindings m_stages[3];

		// Input assembly
		VertexLayout m_vertexLayout;
		VertexFormat m_vertexFormat = VertexFormat::Last;
		const VertexDeclaration* m_vertexDeclaration = nullptr;
		VertexBufferMetal* m_vertexStreams[metal_bindings::MaxVertexStreams] = {};
		bool m_vertexStreamsDirty = true;
		IndexBufferMetal* m_indexBuffer = nullptr;
		uint32 m_indexCount = 0;
		id<MTLBuffer> m_nullVertexBuffer = nil;

		// Fixed vertex format shaders (immediate drawing)
		std::unordered_map<VertexFormat, ShaderPtr> m_formatVertexShaders;
		std::unordered_map<VertexFormat, ShaderPtr> m_formatPixelShaders;

		// Pipeline state
		std::unordered_map<uint64, PipelineEntry> m_pipelineStates;
		std::unordered_map<const void*, ComputePipelineEntry> m_computePipelineStates;
		std::unordered_map<size_t, id<MTLFunction>> m_functionCache;
		std::unordered_map<uint64, id<MTLTexture>> m_placeholderTextures;
		id<MTLBuffer> m_zeroBuffer = nil;
		id<MTLSamplerState> m_defaultSampler = nil;
		const PipelineEntry* m_currentPipelineEntry = nullptr;
		std::unordered_map<uint32, id<MTLDepthStencilState>> m_depthStencilStates;
		std::unordered_map<uint64, id<MTLSamplerState>> m_samplerStates;
		id<MTLRenderPipelineState> m_currentPipeline = nil;
		id<MTLDepthStencilState> m_currentDepthStencil = nil;

		// Fixed function state, applied to the encoder when dirty
		bool m_stencilWrite = false;
		uint8 m_stencilRef = 0;
		float m_depthBias = 0.0f;
		float m_slopeScaledDepthBias = 0.0f;
		float m_depthBiasClamp = 0.0f;
		bool m_scissorEnabled = false;
		MTLScissorRect m_scissorRect {};
		bool m_rasterStateDirty = true;
		bool m_viewportDirty = true;

		// Sampler state
		MTLSamplerMinMagFilter m_samplerMinMag = MTLSamplerMinMagFilterLinear;
		MTLSamplerMipFilter m_samplerMip = MTLSamplerMipFilterLinear;
		uint32 m_samplerAnisotropy = 16;
		MTLSamplerAddressMode m_samplerAddress[3] = { MTLSamplerAddressModeRepeat, MTLSamplerAddressModeRepeat, MTLSamplerAddressModeRepeat };
		bool m_samplerDirty = true;
		id<MTLSamplerState> m_currentSampler = nil;
		uint32 m_maxAnisotropy = 16;
		TextureFilter m_textureFilterCap = TextureFilter::Anisotropic;

		// Matrices
		ConstantBufferPtr m_objectMatrixBuffer;
		ConstantBufferPtr m_viewMatrixBuffer;
		Matrix4 m_inverseView;
		Matrix4 m_restoreInverseView;
		Matrix4 m_inverseProj;
		Matrix4 m_restoreInverseProj;
		bool m_worldMatrixDirty = true;
		bool m_viewMatrixDirty = true;

		// Transient upload memory
		std::vector<TransientChunk> m_transientChunks;
		size_t m_currentTransientChunk = 0;

		MaterialInterface* m_lastBoundMaterial = nullptr;
		PixelShaderType m_lastBoundPixelShaderType = PixelShaderType::Forward;
		std::unordered_map<const VertexDeclaration*, bool> m_blendIndicesCache;

		struct CachedDeclarationLayout
		{
			size_t declarationHash = 0;
			std::vector<VertexLayoutElement> elements;
		};
		std::unordered_map<const VertexDeclaration*, CachedDeclarationLayout> m_declarationLayouts;
		bool m_layoutFromFormat = false;

		bool m_vsync = true;
		void* m_hardwareCursor = nullptr;

		uint64 m_batchCount = 0;
		uint64 m_lastFrameBatchCount = 0;
		uint64 m_totalDrawCount = 0;
		uint64 m_totalPrimitiveCount = 0;
	};
}
