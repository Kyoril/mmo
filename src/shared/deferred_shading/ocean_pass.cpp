// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "ocean_pass.h"

#include "ocean_grid.h"
#include "ocean_spectrum.h"

#include "graphics/global_shader_parameters.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"

#include <algorithm>
#include <cmath>

// --- Shader bytecode seam ---------------------------------------------------------------
// Mirrors the seam in volumetric_fog_pass.cpp; the deferred path is D3D11-only today.
#ifdef _WIN32
#	include <Windows.h>
#	include "shaders/CS_OceanSpectrum.h"
#	include "shaders/CS_OceanEvolve.h"
#	include "shaders/CS_OceanFft.h"
#	include "shaders/CS_OceanAssemble.h"
#	include "shaders/VS_Ocean.h"
#	include "shaders/PS_Ocean.h"
#	define MMO_OCEAN_SHADER(name) g_##name, std::size(g_##name)
#	define MMO_OCEAN_HAS_SHADERS 1
#else
#	define MMO_OCEAN_SHADER(name) nullptr, 0
#	define MMO_OCEAN_HAS_SHADERS 0
#endif
// ---------------------------------------------------------------------------------------

namespace mmo
{
	namespace
	{
		constexpr uint32 threadGroupSize = 8;
		constexpr uint32 spectrumSeed = 20261023;

		/// @brief Vertices per ring of the surface grid.
		constexpr uint32 gridSegments = 320;
		/// @brief Radius of the innermost ring around the camera (m).
		constexpr float gridInnerRadius = 0.35f;
		/// @brief Radius of the outermost ring (m). Far enough that the gap to the true horizon of a
		///        flat sea stays below a pixel for any normal camera height.
		constexpr float gridOuterRadius = 200000.0f;

		/// @brief Distance at which displacement starts to flatten, and how long that takes (m).
		constexpr float waveFadeStart = 1500.0f;
		constexpr float waveFadeRange = 2500.0f;

		/// @brief Mirrors OceanSimConstants in OceanSimCommon.hlsli (b0).
		struct alignas(16) OceanSimConstants
		{
			float cascadeSize[4];
			float bandLow[4];
			float bandHigh[4];

			float windDirection[2];
			float jonswapAlpha;
			float peakOmega;

			float jonswapGamma;
			float spreadExponent;
			float spreadNorm;
			float amplitudeScale;

			float simTime;
			float deltaTime;
			float choppiness;
			float repeatOmega;

			float foamThreshold;
			float foamGrowth;
			float foamDecay;
			uint32 fftPass;

			uint32 seed;
			float shortWaveLength;
			float padding[2];

			float cascadeWeight[4];
		};
		static_assert(sizeof(OceanSimConstants) == 144, "OceanSimConstants must match OceanSimCommon.hlsli");

		/// @brief Mirrors OceanRenderConstants in OceanRenderCommon.hlsli (b4).
		struct alignas(16) OceanRenderConstants
		{
			float cascadeSize[4];
			float cascadeWaveNumber[4];

			float seaLevel;
			float gridAngularStep;
			float shallowDepth;
			float maskEnabled;

			float maskOrigin[2];
			float maskInvExtent;
			float refractionStrength;

			float absorption[3];
			float subsurfaceStrength;

			float troughColor[3];
			float subsurfaceAnisotropy;

			float crestColor[3];
			float ambientScatter;

			float skyHorizon[3];
			float roughness;

			float skyZenith[3];
			float reflectionStrength;

			float sunSpecularStrength;
			float foamBrightness;
			float shoreFoamStrength;
			float shoreFoamDepth;

			float crestHeightScale;
			float oceanTime;
			float waveFadeStart;
			float waveFadeInverseRange;
		};
		static_assert(sizeof(OceanRenderConstants) == 176, "OceanRenderConstants must match OceanRenderCommon.hlsli");

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

		/// @brief CPU twin of InverseTonemap in the generated forward shaders: brings a display-referred
		///        colour (the sky gradient the environment authors) into the linear HDR scene.
		Vector3 inverseTonemap(const Vector4& display, const float exposure)
		{
			const auto channel = [exposure](const float value)
			{
				constexpr float a = 2.51f;
				constexpr float b = 0.03f;
				constexpr float c = 2.43f;
				constexpr float d = 0.59f;
				constexpr float e = 0.14f;
				const float y = std::pow(std::clamp(value, 0.0f, 0.999f), 2.2f);
				const float qa = y * c - a;
				const float qb = y * d - b;
				const float qc = y * e;
				const float x = (-qb - std::sqrt(std::max(qb * qb - 4.0f * qa * qc, 0.0f))) / (2.0f * qa);
				return x / std::max(exposure, 1e-4f);
			};

			return Vector3(channel(display.x), channel(display.y), channel(display.z));
		}

		bool sameWaves(const OceanSettings& a, const OceanSettings& b)
		{
			return a.windSpeed == b.windSpeed && a.windDirection == b.windDirection && a.fetch == b.fetch
				&& a.spread == b.spread && a.waveScale == b.waveScale && a.cascadeSizes == b.cascadeSizes
				&& a.cascadeWeights == b.cascadeWeights && a.shortWaveLength == b.shortWaveLength;
		}
	}

	OceanPass::OceanPass(GraphicsDevice& device)
		: m_device(device)
	{
		if (MMO_OCEAN_HAS_SHADERS)
		{
			CreateResources();
		}
	}

	bool OceanPass::IsSupported() const
	{
		return m_spectrumCs && m_evolveCs && m_fftCs && m_assembleCs && m_surfaceVs && m_surfacePs
			&& m_spectrum && m_fftPing && m_fftPong && m_displacement && m_surfaceTexture && m_foamState
			&& m_wrapSampler && m_clampSampler && m_gridVertices && m_gridIndices;
	}

	void OceanPass::CreateResources()
	{
		m_spectrumCs = m_device.CreateShader(ShaderType::ComputeShader, MMO_OCEAN_SHADER(CS_OceanSpectrum));
		m_evolveCs = m_device.CreateShader(ShaderType::ComputeShader, MMO_OCEAN_SHADER(CS_OceanEvolve));
		m_fftCs = m_device.CreateShader(ShaderType::ComputeShader, MMO_OCEAN_SHADER(CS_OceanFft));
		m_assembleCs = m_device.CreateShader(ShaderType::ComputeShader, MMO_OCEAN_SHADER(CS_OceanAssemble));
		m_surfaceVs = m_device.CreateShader(ShaderType::VertexShader, MMO_OCEAN_SHADER(VS_Ocean));
		m_surfacePs = m_device.CreateShader(ShaderType::PixelShader, MMO_OCEAN_SHADER(PS_Ocean));

		m_simulationBuffer = m_device.CreateConstantBuffer(sizeof(OceanSimConstants), nullptr);
		m_renderBuffer = m_device.CreateConstantBuffer(sizeof(OceanRenderConstants), nullptr);

		constexpr uint16 n = OceanFftResolution;
		constexpr uint16 cascades = OceanCascadeCount;
		m_spectrum = m_device.CreateTextureArray(n, n, cascades, TextureArrayFormat::RGBA32F, true, false);
		m_fftPing = m_device.CreateTextureArray(n, n, cascades * 2, TextureArrayFormat::RGBA32F, true, false);
		m_fftPong = m_device.CreateTextureArray(n, n, cascades * 2, TextureArrayFormat::RGBA32F, true, false);
		m_displacement = m_device.CreateTextureArray(n, n, cascades, TextureArrayFormat::RGBA16F, true, false);
		m_surfaceTexture = m_device.CreateTextureArray(n, n, cascades, TextureArrayFormat::RGBA16F, true, true);
		m_foamState = m_device.CreateTextureArray(n, n, cascades, TextureArrayFormat::R32F, true, false);

		SamplerDesc wrap;
		wrap.filter = SamplerFilter::Anisotropic;
		wrap.address = SamplerAddress::Wrap;
		wrap.maxAnisotropy = 8;
		m_wrapSampler = m_device.CreateSamplerState(wrap);

		SamplerDesc clamp;
		clamp.filter = SamplerFilter::Linear;
		clamp.address = SamplerAddress::Clamp;
		m_clampSampler = m_device.CreateSamplerState(clamp);

		const OceanGrid grid = BuildOceanGrid(gridSegments, gridInnerRadius, gridOuterRadius);
		m_gridVertices = m_device.CreateVertexBuffer(grid.vertices.size() / 3, sizeof(float) * 3, BufferUsage::Static, grid.vertices.data());
		m_gridIndices = m_device.CreateIndexBuffer(grid.indices.size(), IndexBufferSize::Index_32, BufferUsage::Static, grid.indices.data());
		m_gridIndexCount = static_cast<uint32>(grid.indices.size());
		m_gridAngularStep = grid.angularStep;

		if (!IsSupported())
		{
			WLOG("FFT ocean is not supported by this graphics device; terrain ocean water stays meshed");
		}
	}

	void OceanPass::SetSettings(const OceanSettings& settings)
	{
		if (!sameWaves(settings, m_settings))
		{
			m_spectrumDirty = true;
		}

		m_settings = settings;
	}

	void OceanPass::SetSurfaceState(const OceanSurfaceState& state)
	{
		m_surface = state;
	}

	void OceanPass::UploadMask(const std::vector<uint8>& texels, const uint32 texelsPerSide)
	{
		if (texels.empty() || texelsPerSide == 0)
		{
			m_mask.reset();
			m_maskTexelsPerSide = 0;
			return;
		}

		ASSERT(texels.size() == static_cast<size_t>(texelsPerSide) * texelsPerSide * 2);
		if (!m_mask || m_maskTexelsPerSide != texelsPerSide)
		{
			m_mask = m_device.CreateTextureArray(static_cast<uint16>(texelsPerSide), static_cast<uint16>(texelsPerSide), 1, TextureArrayFormat::RG8, false, false);
			m_maskTexelsPerSide = m_mask ? texelsPerSide : 0;
		}

		if (m_mask)
		{
			m_mask->Upload(0, texels.data(), texels.size());
		}
	}

	void OceanPass::UpdateSimulationConstants(const uint32 fftPass)
	{
		const ocean_spectrum::JonswapParameters jonswap = ocean_spectrum::ComputeJonswap(m_settings.windSpeed, m_settings.fetch);
		const auto bands = ocean_spectrum::ComputeCascadeBands(m_settings.cascadeSizes);
		const float windRadians = m_settings.windDirection * ocean_spectrum::Pi / 180.0f;

		OceanSimConstants constants {};
		for (uint32 i = 0; i < OceanCascadeCount; ++i)
		{
			constants.cascadeSize[i] = m_settings.cascadeSizes[i];
			constants.bandLow[i] = bands[i].low;
			constants.bandHigh[i] = bands[i].high;
		}
		constants.windDirection[0] = std::cos(windRadians);
		constants.windDirection[1] = std::sin(windRadians);
		constants.jonswapAlpha = jonswap.alpha;
		constants.peakOmega = jonswap.peakOmega;
		constants.jonswapGamma = ocean_spectrum::JonswapGamma;
		constants.spreadExponent = m_settings.spread;
		constants.spreadNorm = ocean_spectrum::SpreadNormalization(m_settings.spread);
		constants.amplitudeScale = m_settings.waveScale;
		constants.simTime = static_cast<float>(m_time);
		constants.deltaTime = m_deltaTime;
		constants.choppiness = m_settings.choppiness;
		constants.repeatOmega = 2.0f * ocean_spectrum::Pi / ocean_spectrum::RepeatPeriod;
		constants.foamThreshold = 0.5f + m_settings.foamCoverage;
		constants.foamGrowth = m_settings.foamGrowth;
		constants.foamDecay = m_settings.foamDecay;
		constants.fftPass = fftPass;
		constants.seed = spectrumSeed;
		constants.shortWaveLength = m_settings.shortWaveLength;
		for (uint32 i = 0; i < OceanCascadeCount; ++i)
		{
			constants.cascadeWeight[i] = m_settings.cascadeWeights[i];
		}
		m_simulationBuffer->Update(&constants);
	}

	void OceanPass::BuildSpectrum()
	{
		UpdateSimulationConstants(0);
		m_simulationBuffer->BindToStage(ShaderType::ComputeShader, 0);

		m_spectrumCs->Set();
		m_spectrum->BindWritable(0);
		m_device.Dispatch(groupCount(OceanFftResolution), groupCount(OceanFftResolution), OceanCascadeCount);
		m_device.ClearComputeBindings();

		m_spectrumDirty = false;
	}

	void OceanPass::Simulate()
	{
		if (!IsActive())
		{
			m_hasLastTick = false;
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		m_deltaTime = m_hasLastTick ? std::chrono::duration<float>(now - m_lastTick).count() : 0.0f;
		m_deltaTime = std::clamp(m_deltaTime, 0.0f, 0.1f);
		m_lastTick = now;
		m_hasLastTick = true;

		// Wrap at the repeat period: the dispersion is quantised to it, so the sea is identical there
		// and the phase never needs a large time value.
		m_time = std::fmod(m_time + static_cast<double>(m_deltaTime) * m_settings.timeScale, static_cast<double>(ocean_spectrum::RepeatPeriod));

		m_device.BeginGpuScope("GPU: Ocean simulation");

		if (m_spectrumDirty)
		{
			BuildSpectrum();
		}

		const uint32 groups = groupCount(OceanFftResolution);
		constexpr uint32 fftLayers = OceanCascadeCount * 2;

		UpdateSimulationConstants(0);
		m_simulationBuffer->BindToStage(ShaderType::ComputeShader, 0);

		m_evolveCs->Set();
		m_spectrum->Bind(ShaderType::ComputeShader, 0);
		m_fftPing->BindWritable(0);
		m_device.Dispatch(groups, groups, OceanCascadeCount);
		m_device.ClearComputeBindings();

		// Rows: ping -> pong.
		m_fftCs->Set();
		m_fftPing->Bind(ShaderType::ComputeShader, 0);
		m_fftPong->BindWritable(0);
		m_device.Dispatch(1, OceanFftResolution, fftLayers);
		m_device.ClearComputeBindings();

		// Columns: pong -> ping.
		UpdateSimulationConstants(1);
		m_fftCs->Set();
		m_fftPong->Bind(ShaderType::ComputeShader, 0);
		m_fftPing->BindWritable(0);
		m_device.Dispatch(1, OceanFftResolution, fftLayers);
		m_device.ClearComputeBindings();

		m_assembleCs->Set();
		m_fftPing->Bind(ShaderType::ComputeShader, 0);
		m_displacement->BindWritable(0);
		m_surfaceTexture->BindWritable(1);
		m_foamState->BindWritable(2);
		m_device.Dispatch(groups, groups, OceanCascadeCount);
		m_device.ClearComputeBindings();

		m_surfaceTexture->GenerateMips();

		m_device.EndGpuScope();
	}

	void OceanPass::Render(Camera& camera, ConstantBuffer& cameraBuffer, const float exposure)
	{
		if (!IsActive())
		{
			return;
		}

		m_device.BeginGpuScope("GPU: Ocean surface");

		const float hs = ocean_spectrum::SignificantWaveHeight(m_settings.windSpeed, m_settings.fetch) * m_settings.waveScale;
		const bool masked = m_mask != nullptr && m_maskTexelsPerSide > 0;

		OceanRenderConstants constants {};
		for (uint32 i = 0; i < OceanCascadeCount; ++i)
		{
			constants.cascadeSize[i] = m_settings.cascadeSizes[i];
			constants.cascadeWaveNumber[i] = 2.0f * ocean_spectrum::Pi / std::max(m_settings.cascadeSizes[i], 0.01f);
		}
		constants.seaLevel = m_surface.seaLevel;
		constants.gridAngularStep = m_gridAngularStep;
		constants.shallowDepth = m_settings.shallowDepth;
		constants.maskEnabled = masked ? 1.0f : 0.0f;
		constants.maskOrigin[0] = m_surface.maskOriginX;
		constants.maskOrigin[1] = m_surface.maskOriginZ;
		constants.maskInvExtent = masked ? 1.0f / (m_surface.maskTexelSize * static_cast<float>(m_maskTexelsPerSide)) : 0.0f;
		constants.refractionStrength = m_settings.refractionStrength;
		copyVector(m_settings.absorption, constants.absorption);
		constants.subsurfaceStrength = m_settings.subsurfaceStrength;
		copyVector(m_settings.troughColor, constants.troughColor);
		constants.subsurfaceAnisotropy = m_settings.subsurfaceAnisotropy;
		copyVector(m_settings.crestColor, constants.crestColor);
		constants.ambientScatter = m_settings.ambientScatter;

		GlobalShaderParameters& globals = GlobalShaderParameters::Get();
		Vector4 horizon(0.62f, 0.76f, 0.94f, 1.0f);
		Vector4 zenith(0.24f, 0.45f, 0.82f, 1.0f);
		globals.GetVector("SkyHorizonColor", horizon);
		globals.GetVector("SkyZenithColor", zenith);
		copyVector(inverseTonemap(horizon, exposure), constants.skyHorizon);
		copyVector(inverseTonemap(zenith, exposure), constants.skyZenith);

		constants.roughness = m_settings.roughness;
		constants.reflectionStrength = m_settings.reflectionStrength;
		constants.sunSpecularStrength = m_settings.sunSpecularStrength;
		constants.foamBrightness = m_settings.foamBrightness;
		constants.shoreFoamStrength = m_settings.shoreFoamStrength;
		constants.shoreFoamDepth = m_settings.shoreFoamDepth;
		constants.crestHeightScale = 1.0f / std::max(hs * 0.5f, 0.05f);
		constants.oceanTime = static_cast<float>(m_time);
		constants.waveFadeStart = waveFadeStart;
		constants.waveFadeInverseRange = 1.0f / waveFadeRange;
		m_renderBuffer->Update(&constants);

		m_device.SetTransformMatrix(World, Matrix4::Identity);
		m_device.SetTransformMatrix(View, camera.GetViewMatrix());
		m_device.SetTransformMatrix(Projection, camera.GetProjectionMatrix());

		m_device.SetBlendMode(BlendMode::Opaque);
		m_device.SetDepthEnabled(true);
		m_device.SetDepthWriteEnabled(true);
		m_device.SetDepthTestComparison(DepthTestMethod::LessEqual);
		m_device.SetFillMode(FillMode::Solid);
		m_device.SetFaceCullMode(FaceCullMode::None);

		// SetVertexFormat installs the format's default vertex shader, so the ocean's comes after it.
		m_device.SetVertexFormat(VertexFormat::Pos);
		m_device.SetTopologyType(TopologyType::TriangleList);
		m_surfaceVs->Set();
		m_surfacePs->Set();

		cameraBuffer.BindToStage(ShaderType::VertexShader, 1);
		cameraBuffer.BindToStage(ShaderType::PixelShader, 1);
		m_renderBuffer->BindToStage(ShaderType::VertexShader, 4);
		m_renderBuffer->BindToStage(ShaderType::PixelShader, 4);

		m_displacement->Bind(ShaderType::VertexShader, 0);
		m_surfaceTexture->Bind(ShaderType::PixelShader, 2);
		if (masked)
		{
			m_mask->Bind(ShaderType::VertexShader, 1);
			m_mask->Bind(ShaderType::PixelShader, 1);
		}

		m_wrapSampler->Bind(ShaderType::VertexShader, 2);
		m_wrapSampler->Bind(ShaderType::PixelShader, 2);
		m_clampSampler->Bind(ShaderType::VertexShader, 3);
		m_clampSampler->Bind(ShaderType::PixelShader, 3);

		m_gridVertices->Set(0);
		m_gridIndices->Set(0);
		m_device.DrawIndexed(0, m_gridIndexCount);

		// The displacement and surface arrays are compute targets next frame.
		m_displacement->Unbind(ShaderType::VertexShader, 0);
		m_surfaceTexture->Unbind(ShaderType::PixelShader, 2);

		// The ocean replaced textures, samplers and constant buffers a material may consider its own;
		// make the next material draw rebind everything.
		m_device.InvalidateBoundMaterial();

		m_device.EndGpuScope();
	}
}
