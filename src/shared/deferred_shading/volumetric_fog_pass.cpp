// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "volumetric_fog_pass.h"

#include "fog_noise.h"

#include "log/default_log_levels.h"
#include "scene_graph/camera.h"

// --- Shader bytecode seam ---------------------------------------------------------------
// Mirrors the seam in ssao_pass.cpp; the deferred path is D3D11-only today.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/CS_FogInject.h"
#	include "shaders/CS_FogTemporal.h"
#	include "shaders/CS_FogIntegrate.h"
#	include "shaders/PS_FogComposite.h"
#	define MMO_FOG_INJECT_CS_BYTECODE g_CS_FogInject
#	define MMO_FOG_INJECT_CS_SIZE std::size(g_CS_FogInject)
#	define MMO_FOG_TEMPORAL_CS_BYTECODE g_CS_FogTemporal
#	define MMO_FOG_TEMPORAL_CS_SIZE std::size(g_CS_FogTemporal)
#	define MMO_FOG_INTEGRATE_CS_BYTECODE g_CS_FogIntegrate
#	define MMO_FOG_INTEGRATE_CS_SIZE std::size(g_CS_FogIntegrate)
#	define MMO_FOG_COMPOSITE_PS_BYTECODE g_PS_FogComposite
#	define MMO_FOG_COMPOSITE_PS_SIZE std::size(g_PS_FogComposite)
#else
#	define MMO_FOG_INJECT_CS_BYTECODE nullptr
#	define MMO_FOG_INJECT_CS_SIZE 0
#	define MMO_FOG_TEMPORAL_CS_BYTECODE nullptr
#	define MMO_FOG_TEMPORAL_CS_SIZE 0
#	define MMO_FOG_INTEGRATE_CS_BYTECODE nullptr
#	define MMO_FOG_INTEGRATE_CS_SIZE 0
#	define MMO_FOG_COMPOSITE_PS_BYTECODE nullptr
#	define MMO_FOG_COMPOSITE_PS_SIZE 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		constexpr uint32 noiseSeed = 1337;
		constexpr uint32 threadGroupSize = 8;

		/// @brief Mirrors VolumetricFogBuffer in VolumetricFogCommon.hlsli (b2).
		struct alignas(16) VolumetricFogConstants
		{
			Matrix4 inverseViewProj;
			Matrix4 prevViewProj;

			float cameraForward[3];
			float jitter;

			float prevCameraPosition[3];
			float temporalBlend;

			float prevCameraForward[3];
			float historyValid;

			uint32 gridWidth;
			uint32 gridHeight;
			uint32 gridDepth;
			uint32 debugMode;

			float nearDistance;
			float farDistance;
			float noiseSize;
			float noiseAmount;

			float windOffsetX;
			float windOffsetZ;
			float skyDistance;
			float volumeEnabled;

			uint32 lightCount;
			float lightScatterStrength;
			float lightPadding[2];

			uint32 fogVolumeCount;
			float fogVolumePadding[3];
		};

		static_assert(sizeof(VolumetricFogConstants) == 256, "VolumetricFogConstants must match the HLSL layout");

		uint32 groupCount(const uint32 cells)
		{
			return (cells + threadGroupSize - 1) / threadGroupSize;
		}

		void copyVector(const Vector3& source, float destination[3])
		{
			destination[0] = source.x;
			destination[1] = source.y;
			destination[2] = source.z;
		}
	}

	VolumetricFogPass::VolumetricFogPass(GraphicsDevice& device, const uint32 width, const uint32 height)
		: m_device(device)
		, m_width(width)
		, m_height(height)
	{
		m_fogBuffer = m_device.CreateConstantBuffer(sizeof(VolumetricFogConstants), nullptr);
		ASSERT(m_fogBuffer);

		m_fogVolumeBuffer = m_device.CreateStructuredBuffer(sizeof(fog_volume::GpuFogVolume), fog_volume::MaxVolumesPerFrame, nullptr);
		if (!m_fogVolumeBuffer)
		{
			// The null graphics device (used headlessly, e.g. in tests/tools) returns nullptr here.
			// SetFogVolumes and Render already tolerate a null buffer by skipping the volume upload
			// and binding, so this is not fatal - just fewer fog volumes rendered than requested.
			WLOG("Failed to create fog volume structured buffer - local fog volumes will not be rendered");
		}
		m_gpuFogVolumes.reserve(fog_volume::MaxVolumesPerFrame);

		m_compositePs = m_device.CreateShader(ShaderType::PixelShader, MMO_FOG_COMPOSITE_PS_BYTECODE, MMO_FOG_COMPOSITE_PS_SIZE);
		ASSERT(m_compositePs);

		if (MMO_FOG_INJECT_CS_SIZE > 0)
		{
			m_injectCs = m_device.CreateShader(ShaderType::ComputeShader, MMO_FOG_INJECT_CS_BYTECODE, MMO_FOG_INJECT_CS_SIZE);
			m_temporalCs = m_device.CreateShader(ShaderType::ComputeShader, MMO_FOG_TEMPORAL_CS_BYTECODE, MMO_FOG_TEMPORAL_CS_SIZE);
			m_integrateCs = m_device.CreateShader(ShaderType::ComputeShader, MMO_FOG_INTEGRATE_CS_BYTECODE, MMO_FOG_INTEGRATE_CS_SIZE);
		}

		SamplerDesc linearClamp;
		linearClamp.filter = SamplerFilter::Linear;
		linearClamp.address = SamplerAddress::Clamp;
		m_linearClampSampler = m_device.CreateSamplerState(linearClamp);

		SamplerDesc noiseWrap;
		noiseWrap.filter = SamplerFilter::Linear;
		noiseWrap.address = SamplerAddress::Wrap;
		m_noiseSampler = m_device.CreateSamplerState(noiseWrap);

		constexpr uint32 noiseSize = VolumetricFogSettings::NoiseResolution;
		m_noiseVolume = m_device.CreateVolumeTexture(noiseSize, noiseSize, noiseSize, VolumeFormat::R8, false);
		if (m_noiseVolume)
		{
			const std::vector<uint8> noise = GenerateFogNoise(noiseSize, noiseSeed);
			m_noiseVolume->Upload(noise.data(), noise.size());
		}
	}

	void VolumetricFogPass::Resize(const uint32 width, const uint32 height)
	{
		m_width = width;
		m_height = height;
		ReleaseVolumes();
	}

	void VolumetricFogPass::SetFogVolumes(const std::vector<FogVolumeInstance>& volumes)
	{
		const size_t count = std::min(volumes.size(), static_cast<size_t>(fog_volume::MaxVolumesPerFrame));

		m_gpuFogVolumes.clear();
		for (size_t i = 0; i < count; ++i)
		{
			m_gpuFogVolumes.push_back(fog_volume::ToGpu(volumes[i]));
		}

		m_fogVolumeCount = 0;
		if (!m_gpuFogVolumes.empty() && m_fogVolumeBuffer)
		{
			m_fogVolumeBuffer->Update(m_gpuFogVolumes.data(), m_gpuFogVolumes.size());
			m_fogVolumeCount = static_cast<uint32>(m_gpuFogVolumes.size());
		}
	}

	bool VolumetricFogPass::SupportsVolume() const
	{
		return m_injectCs && m_temporalCs && m_integrateCs && m_noiseVolume && m_linearClampSampler && m_noiseSampler;
	}

	void VolumetricFogPass::ReleaseVolumes()
	{
		m_injectVolume.reset();
		m_historyVolumes[0].reset();
		m_historyVolumes[1].reset();
		m_gridWidth = 0;
		m_gridHeight = 0;
		m_gridDepth = 0;
		m_historyValid = false;
	}

	void VolumetricFogPass::EnsureVolumes()
	{
		const uint32 gridWidth = m_settings.GetGridWidth(m_width);
		const uint32 gridHeight = m_settings.GetGridHeight(m_height);
		const uint32 gridDepth = m_settings.sliceCount;

		if (m_injectVolume && gridWidth == m_gridWidth && gridHeight == m_gridHeight && gridDepth == m_gridDepth)
		{
			return;
		}

		if (gridWidth == m_failedGridWidth && gridHeight == m_failedGridHeight && gridDepth == m_failedGridDepth)
		{
			// The exact same grid size already failed to allocate; retrying every frame under the same VRAM
			// pressure that caused the failure would just repeat the driver hit for no gain. Render already
			// treats a null m_injectVolume as "closed-form fog only", so leave it that way until the desired
			// size actually changes (quality preset change or Resize).
			return;
		}

		// Desired size differs from the last remembered failure (or there was none yet): allow a fresh
		// attempt, and let a failure at this new size log again.
		m_failedGridWidth = 0;
		m_failedGridHeight = 0;
		m_failedGridDepth = 0;
		m_volumeAllocationFailureLogged = false;

		const auto create = [this, gridWidth, gridHeight, gridDepth]()
		{
			return m_device.CreateVolumeTexture(static_cast<uint16>(gridWidth), static_cast<uint16>(gridHeight), static_cast<uint16>(gridDepth), VolumeFormat::RGBA16F, true);
		};

		VolumeTexturePtr injectVolume = create();
		VolumeTexturePtr historyVolume0 = create();
		VolumeTexturePtr historyVolume1 = create();

		if (!injectVolume || !historyVolume0 || !historyVolume1)
		{
			// Allocation failure (e.g. exhausted VRAM at a high quality preset): fall back to no volume
			// rather than leaving a partially-populated set of volumes for Render to bind. Render treats
			// a missing m_injectVolume as "disabled this frame" and runs the closed-form fog only.
			if (!m_volumeAllocationFailureLogged)
			{
				WLOG("Volumetric fog: failed to allocate " << gridWidth << "x" << gridHeight << "x" << gridDepth
					<< " RGBA16F grid volumes - falling back to closed-form fog only");
				m_volumeAllocationFailureLogged = true;
			}

			m_failedGridWidth = gridWidth;
			m_failedGridHeight = gridHeight;
			m_failedGridDepth = gridDepth;

			ReleaseVolumes();
			return;
		}

		m_injectVolume = std::move(injectVolume);
		m_historyVolumes[0] = std::move(historyVolume0);
		m_historyVolumes[1] = std::move(historyVolume1);

		m_gridWidth = gridWidth;
		m_gridHeight = gridHeight;
		m_gridDepth = gridDepth;
		m_historyIndex = 0;
		m_historyValid = false;
	}

	void VolumetricFogPass::Render(Camera& camera, const WindState& wind, RenderTexture& sceneColor, RenderTexture& gbufferNormalRT, RenderTexture& output,
		const std::array<RenderTexturePtr, NUM_SHADOW_CASCADES>& cascadeShadowMaps, ConstantBuffer& shadowBuffer,
		ConstantBuffer& cameraBuffer, StructuredBuffer& lights, const uint32 lightCount, SamplerState& shadowSampler, VertexBuffer& quad,
		ShaderBase& fullscreenVs)
	{
		// Blend state persists across frames and the previous forward pass may have left alpha blending
		// on; the composite must overwrite its target.
		m_device.SetBlendMode(BlendMode::Opaque);

		bool volumeEnabled = m_settings.IsVolumeEnabled() && SupportsVolume();
		if (volumeEnabled)
		{
			EnsureVolumes();
			// EnsureVolumes releases the volumes and returns without them on allocation failure; treat
			// that the same as the volume being disabled for this frame (closed-form fog only).
			volumeEnabled = m_injectVolume != nullptr;
		}
		else if (m_injectVolume)
		{
			ReleaseVolumes();
		}

		const Matrix4 viewProj = camera.GetProjectionMatrix() * camera.GetViewMatrix();
		const Vector3 cameraPosition = camera.GetDerivedPosition();
		const Vector3 cameraForward = camera.GetDerivedDirection().NormalizedCopy();

		const float clampedRange = std::max(m_settings.range, VolumetricFogSettings::MinRange);

		const float resetDistance = VolumetricFogSettings::HistoryResetDistance;
		if ((cameraPosition - m_prevCameraPosition).GetSquaredLength() > resetDistance * resetDistance || clampedRange != m_prevRange)
		{
			m_historyValid = false;
		}

		VolumetricFogConstants constants{};
		constants.inverseViewProj = viewProj.Inverse();
		constants.prevViewProj = m_prevViewProj;
		copyVector(cameraForward, constants.cameraForward);
		constants.jitter = volumetric_fog::JitterForFrame(m_frameIndex);
		copyVector(m_prevCameraPosition, constants.prevCameraPosition);
		constants.temporalBlend = VolumetricFogSettings::TemporalBlend;
		copyVector(m_prevCameraForward, constants.prevCameraForward);
		constants.historyValid = m_historyValid ? 1.0f : 0.0f;
		constants.gridWidth = m_gridWidth;
		constants.gridHeight = m_gridHeight;
		constants.gridDepth = m_gridDepth;
		constants.debugMode = m_settings.debugMode;
		constants.nearDistance = VolumetricFogSettings::NearDistance;
		constants.farDistance = clampedRange;
		constants.noiseSize = wind.noiseSize;
		constants.noiseAmount = wind.noiseAmount;
		constants.windOffsetX = wind.noiseOffsetX;
		constants.windOffsetZ = wind.noiseOffsetZ;
		constants.skyDistance = VolumetricFogSettings::SkyDistance;
		constants.volumeEnabled = volumeEnabled ? 1.0f : 0.0f;
		constants.lightCount = lightCount;
		constants.lightScatterStrength = m_settings.lightScatterStrength;
		constants.lightPadding[0] = 0.0f;
		constants.lightPadding[1] = 0.0f;
		constants.fogVolumeCount = m_fogVolumeCount;
		m_fogBuffer->Update(&constants);

		if (volumeEnabled)
		{
			cameraBuffer.BindToStage(ShaderType::ComputeShader, 1);
			m_fogBuffer->BindToStage(ShaderType::ComputeShader, 2);
			shadowBuffer.BindToStage(ShaderType::ComputeShader, 3);

			// --- Inject -----------------------------------------------------------------------
			m_noiseVolume->Bind(ShaderType::ComputeShader, 0);
			for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
			{
				cascadeShadowMaps[i]->Bind(ShaderType::ComputeShader, 5 + i);
			}
			m_noiseSampler->Bind(ShaderType::ComputeShader, 0);
			shadowSampler.Bind(ShaderType::ComputeShader, 1);
			lights.BindToStage(ShaderType::ComputeShader, 9);
			if (m_fogVolumeCount > 0)
			{
				// The inject shader only reads t10 while FogVolumeCount > 0.
				m_fogVolumeBuffer->BindToStage(ShaderType::ComputeShader, 10);
			}
			m_injectVolume->BindWritable(0);
			m_injectCs->Set();
			m_device.Dispatch(groupCount(m_gridWidth), groupCount(m_gridHeight), groupCount(m_gridDepth));
			m_device.ClearComputeBindings();

			// --- Temporal ---------------------------------------------------------------------
			const VolumeTexturePtr& temporalTarget = m_historyVolumes[m_historyIndex];
			const VolumeTexturePtr& history = m_historyVolumes[1 - m_historyIndex];
			m_injectVolume->Bind(ShaderType::ComputeShader, 0);
			history->Bind(ShaderType::ComputeShader, 1);
			m_linearClampSampler->Bind(ShaderType::ComputeShader, 0);
			temporalTarget->BindWritable(0);
			m_temporalCs->Set();
			m_device.Dispatch(groupCount(m_gridWidth), groupCount(m_gridHeight), groupCount(m_gridDepth));
			m_device.ClearComputeBindings();

			// --- Integrate --------------------------------------------------------------------
			// m_injectVolume was only read by the temporal step above and is free by now, so the
			// integrate step writes its front-to-back accumulation back into it instead of a fourth
			// volume.
			temporalTarget->Bind(ShaderType::ComputeShader, 0);
			m_injectVolume->BindWritable(0);
			m_integrateCs->Set();
			m_device.Dispatch(groupCount(m_gridWidth), groupCount(m_gridHeight), 1);
			m_device.ClearComputeBindings();
		}

		// --- Composite ------------------------------------------------------------------------
		m_device.SetDepthEnabled(false);
		m_device.SetDepthWriteEnabled(false);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);
		m_device.SetVertexFormat(VertexFormat::PosColorTex1);
		m_device.SetTopologyType(TopologyType::TriangleList);

		fullscreenVs.Set();
		quad.Set(0);
		cameraBuffer.BindToStage(ShaderType::PixelShader, 1);
		m_fogBuffer->BindToStage(ShaderType::PixelShader, 2);

		output.Activate();
		m_device.SetViewport(0, 0, static_cast<int32>(m_width), static_cast<int32>(m_height), 0.0f, 1.0f);

		// GraphicsDeviceD3D11::Draw() calls UpdateSamplerState(), which may rebind PS s0 from the device's
		// legacy sampler cache; match that cache to the explicit LinearClamp sampler bound below so the two
		// don't disagree at s0 after this pass runs.
		m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		m_device.SetTextureFilter(TextureFilter::Bilinear);

		sceneColor.Bind(ShaderType::PixelShader, 0);
		gbufferNormalRT.Bind(ShaderType::PixelShader, 1);
		if (volumeEnabled)
		{
			// m_injectVolume holds this frame's integrated result (see the Integrate step above).
			m_injectVolume->Bind(ShaderType::PixelShader, 2);
			m_historyVolumes[m_historyIndex]->Bind(ShaderType::PixelShader, 3);
		}

		m_compositePs->Set();

		// Bound last; matches the device state set above so the legacy sampler cache and this explicit
		// sampler agree at s0.
		if (m_linearClampSampler)
		{
			m_linearClampSampler->Bind(ShaderType::PixelShader, 0);
		}

		m_device.Draw(6, 0);

		// Cache hygiene only, not a hazard fix: this clears these slots from the device's texture bind
		// cache. The next frame's lighting pass rebinds t2/t3 to its own targets before this pass's
		// compute steps write these volumes again, so nothing downstream depends on the clear itself.
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 0);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 1);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 2);
		m_device.BindTexture(nullptr, ShaderType::PixelShader, 3);

		// --- Frame bookkeeping ----------------------------------------------------------------
		if (volumeEnabled)
		{
			m_historyIndex = 1 - m_historyIndex;
			m_historyValid = true;
		}

		m_prevViewProj = viewProj;
		m_prevCameraPosition = cameraPosition;
		m_prevCameraForward = cameraForward;
		m_prevRange = clampedRange;
		++m_frameIndex;
	}
}
