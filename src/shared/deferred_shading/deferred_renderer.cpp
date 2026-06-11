// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "deferred_renderer.h"
#include "cascaded_shadow_camera_setup.h"

#include "frame_ui/rect.h"
#include "graphics/graphics_device.h"
#include "graphics/shader_compiler.h"
#include "graphics/structured_buffer.h"
#include "scene_graph/camera.h"
#include "scene_graph/render_queue.h"
#include "log/default_log_levels.h"
#include "base/profiler.h"

#ifdef WIN32
#   include <Windows.h>
#   include "shaders/VS_DeferredLighting.h"
#   include "shaders/PS_DeferredLighting.h"
#   include "graphics_d3d11/graphics_device_d3d11.h"
#   include "graphics_d3d11/render_texture_d3d11.h"
#endif

namespace mmo
{
    // Light structure that matches the one in the shader (StructuredBuffer element)
    struct alignas(16) ShaderLight
    {
        Vector3 position;
        float range;
        Vector3 color;
        float intensity;
        Vector3 direction;
        float spotAngle;
        uint32 type;  // 0 = Point, 1 = Directional, 2 = Spot
        int32 shadowMap;
        Vector2 padding;
    };

	struct alignas(16) ShadowBuffer
    {
        // Cascade view-projection matrices
        Matrix4 cascadeViewProjections[NUM_SHADOW_CASCADES];
        
        // Cascade split distances (in view space)
        Vector4 cascadeSplitDistances;
        
        float shadowBias;           // Depth bias to prevent shadow acne
        float normalBiasScale;      // Bias scale factor based on normal
        float shadowSoftness;       // Controls general softness of shadows  
        float blockerSearchRadius;  // Search radius for the blocker search phase
        
        float lightSize;            // Controls the size of the virtual light (larger = softer shadows)
        uint32 cascadeCount;        // Number of active cascades
        uint32 debugCascades;       // Whether to show cascade debug colors
        float cascadeBlendFactor;   // Blend factor for cascade transitions

        uint32 pcfSampleCount;      // Number of PCF taps per shadow lookup (shadow quality)
        float shadowPadding0;
        float shadowPadding1;
        float shadowPadding2;
    };

    // Light metadata constant buffer - small struct with just count and ambient color
    struct alignas(16) LightMetadata
    {
        uint32 lightCount;
        Vector3 ambientColor;
    };

	DeferredRenderer::DeferredRenderer(GraphicsDevice& device, Scene& scene, uint32 width, uint32 height)
        : m_device(device)
		, m_scene(scene)
        , m_gBuffer(device, width, height)
    {
        m_shadowCameraSetup = std::make_shared<DefaultShadowCameraSetup>();
        m_cascadedShadowSetup = std::make_shared<CascadedShadowCameraSetup>();
        
        // Configure CSM defaults for high quality
        auto& csmConfig = m_cascadedShadowSetup->GetConfig();
        csmConfig.maxShadowDistance = 300.0f;
        csmConfig.shadowMapSize = m_shadowMapSize;
        csmConfig.splitLambda = 0.72f;  // Mostly logarithmic for better near-camera detail
        csmConfig.stableCascades = true;
        csmConfig.cascadeBlendFactor = 0.25f;
        
#ifdef WIN32
		m_deferredLightVs = m_device.CreateShader(ShaderType::VertexShader, g_VS_DeferredLighting, std::size(g_VS_DeferredLighting));
        m_deferredLightPs = m_device.CreateShader(ShaderType::PixelShader, g_PS_DeferredLighting, std::size(g_PS_DeferredLighting));
#endif
        // Create the light metadata constant buffer (small - just count and ambient color)
        m_lightMetadataBuffer = m_device.CreateConstantBuffer(sizeof(LightMetadata), nullptr);
        
        // Create the structured buffer for lights (can hold many more lights than a constant buffer)
        m_lightStructuredBuffer = m_device.CreateStructuredBuffer(sizeof(ShaderLight), MAX_LIGHTS, nullptr);
        
        m_shadowBuffer = m_device.CreateConstantBuffer(sizeof(ShadowBuffer), nullptr);

        // Create the render texture with 16-bit per channel format to reduce banding
        m_renderTexture = m_device.CreateRenderTexture("DeferredOutput", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, R16G16B16A16);
        ASSERT(m_renderTexture);

        // Scene color copy (same HDR format) used as the refraction source for translucent materials.
        m_sceneColorCopy = m_device.CreateRenderTexture("SceneColorCopy", width, height, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::ShaderResourceView, R16G16B16A16);
        ASSERT(m_sceneColorCopy);

        const uint32 color = 0xFFFFFFFF;
        const POS_COL_TEX_VERTEX vertices[6]{
            { { -1.0f,	-1.0f,		0.0f }, color, { 0.0f, 1.0f } },
            { { -1.0f,	1.0f,		0.0f }, color, { 0.0f, 0.0f } },
            { { 1.0f,	1.0f,		0.0f }, color, { 1.0f, 0.0f } },
            { { 1.0f,	1.0f,		0.0f }, color, { 1.0f, 0.0f } },
            { { 1.0f,	-1.0f,		0.0f }, color, { 1.0f, 1.0f } },
            { { -1.0f,	-1.0f,		0.0f }, color, { 0.0f, 1.0f } }
        };

        m_quadBuffer = m_device.CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::StaticWriteOnly, vertices);
        
		// Create shadow maps for each cascade
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            m_cascadeShadowMaps[i] = m_device.CreateRenderTexture(
                "ShadowMapCascade" + std::to_string(i), 
                m_shadowMapSize, 
                m_shadowMapSize, 
                RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
        }
        
        // Keep a reference to the first cascade for legacy single-shadow-map code paths
		m_shadowMapRT = m_cascadeShadowMaps[0];

        // Setup shadow cameras for each cascade
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            m_shadowCameraNodes[i] = m_scene.GetRootSceneNode().CreateChildSceneNode("__ShadowCameraNode_" + std::to_string(i) + "__");
            m_shadowCameras[i] = m_scene.CreateCamera("__DeferredShadowCamera_" + std::to_string(i) + "__");
            m_shadowCameraNodes[i]->AttachObject(*m_shadowCameras[i]);
        }

#ifdef WIN32        // TODO: Fix me: Move me to graphicsd3d11
        D3D11_SAMPLER_DESC sampDesc = {};
        // Using anisotropic filtering for better quality at oblique angles
        sampDesc.Filter = D3D11_FILTER_COMPARISON_ANISOTROPIC;
        // Use border addressing to avoid shadow edge artifacts
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;  // Shadow test: fragment depth < stored depth
        // White border color to avoid darkening at edges
        sampDesc.BorderColor[0] = 1.0f;
        sampDesc.BorderColor[1] = 1.0f;
        sampDesc.BorderColor[2] = 1.0f;
        sampDesc.BorderColor[3] = 1.0f;
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        // Increase anisotropy level for better quality
        sampDesc.MaxAnisotropy = 4;
        sampDesc.MipLODBias = 0;

		GraphicsDeviceD3D11& d3ddev = (GraphicsDeviceD3D11&)device;
        ID3D11Device& d3d11dev = d3ddev;
        d3d11dev.CreateSamplerState(&sampDesc, &m_shadowSampler);
#endif
    }

    DeferredRenderer::~DeferredRenderer()
    {
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            if (m_shadowCameras[i])
            {
                m_scene.DestroyCamera(*m_shadowCameras[i]);
                m_shadowCameras[i] = nullptr;
            }

            if (m_shadowCameraNodes[i])
            {
                m_scene.DestroySceneNode(*m_shadowCameraNodes[i]);
                m_shadowCameraNodes[i] = nullptr;
            }
        }
    }

#ifdef _WIN32
    void DeferredRenderer::InitGpuTimers()
    {
        if (m_gpuTimersInitialized)
        {
            return;
        }

        GraphicsDeviceD3D11& d3dDev = static_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
        ID3D11Device& dev = d3dDev;

        for (uint32 f = 0; f < GpuTimerFrameCount; ++f)
        {
            D3D11_QUERY_DESC disjointDesc = {};
            disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
            dev.CreateQuery(&disjointDesc, m_gpuDisjointQueries[f].GetAddressOf());

            for (uint32 p = 0; p < GpuTimerPointCount; ++p)
            {
                D3D11_QUERY_DESC tsDesc = {};
                tsDesc.Query = D3D11_QUERY_TIMESTAMP;
                dev.CreateQuery(&tsDesc, m_gpuTimestampQueries[f][p].GetAddressOf());
            }
        }

        m_gpuTimersInitialized = true;
    }

    void DeferredRenderer::GpuTimerBegin()
    {
        InitGpuTimers();

        GraphicsDeviceD3D11& d3dDev = static_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
        ID3D11DeviceContext& ctx = d3dDev;

        const uint32 frame = m_gpuTimerWriteFrame;
        ctx.Begin(m_gpuDisjointQueries[frame].Get());
        ctx.End(m_gpuTimestampQueries[frame][0].Get()); // start timestamp
    }

    void DeferredRenderer::GpuTimerMark(const uint32 point)
    {
        if (point >= GpuTimerPointCount)
        {
            return;
        }

        GraphicsDeviceD3D11& d3dDev = static_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
        ID3D11DeviceContext& ctx = d3dDev;
        ctx.End(m_gpuTimestampQueries[m_gpuTimerWriteFrame][point].Get());
    }

    void DeferredRenderer::GpuTimerEndAndCollect()
    {
        GraphicsDeviceD3D11& d3dDev = static_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
        ID3D11DeviceContext& ctx = d3dDev;

        auto& profiler = Profiler::GetInstance();

        const uint32 frame = m_gpuTimerWriteFrame;
        ctx.End(m_gpuDisjointQueries[frame].Get());
        m_gpuTimerFrameStarted[frame] = true;

        // Read back a frame several frames in the past so the GPU is never stalled and all timestamps
        // have resolved (some drivers report the disjoint query complete slightly before the last few
        // timestamps in the region are retrievable, which is why we read well behind the write head).
        const uint32 readFrame = (frame + 1) % GpuTimerFrameCount;
        if (m_gpuTimerFrameStarted[readFrame])
        {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
            const HRESULT djHr = ctx.GetData(m_gpuDisjointQueries[readFrame].Get(), &disjoint, sizeof(disjoint), 0);

            if (djHr == S_OK && disjoint.Frequency != 0)
            {
                UINT64 timestamps[GpuTimerPointCount] = {};
                bool ready[GpuTimerPointCount] = {};
                for (uint32 p = 0; p < GpuTimerPointCount; ++p)
                {
                    ready[p] = (ctx.GetData(m_gpuTimestampQueries[readFrame][p].Get(), &timestamps[p], sizeof(UINT64), 0) == S_OK);
                }

                const double freq = static_cast<double>(disjoint.Frequency);
                auto emit = [&](const char* name, uint32 a, uint32 b)
                {
                    if (ready[a] && ready[b])
                    {
                        const double ms = (timestamps[b] > timestamps[a])
                            ? (static_cast<double>(timestamps[b] - timestamps[a]) / freq) * 1000.0 : 0.0;
                        profiler.AddTime(name, ms);
                    }
                };

                // Named with a "GPU: " prefix so they group together in the perf overlay. Each pass is
                // emitted independently so partial data still shows even if some timestamps lag.
                emit("GPU: Shadows", 0, 1);
                emit("GPU: GBuffer", 1, 2);
                emit("GPU: Lighting", 2, 3);
                emit("GPU: Forward", 3, 4);
                emit("GPU: Total (passes)", 0, 4);
            }
        }

        m_gpuTimerWriteFrame = (frame + 1) % GpuTimerFrameCount;
    }
#endif

    void DeferredRenderer::Resize(uint32 width, uint32 height)
    {
        // Resize the G-Buffer
        m_gBuffer.Resize(width, height);
		m_renderTexture->Resize(width, height);
		m_sceneColorCopy->Resize(width, height);
    }

    void DeferredRenderer::Render(Scene& scene, Camera& camera)
    {
        // Update the scene graph first to ensure all transforms are up-to-date
        scene.UpdateSceneGraph();

#ifdef _WIN32
        // Per-pass GPU timing is only collected while the perf overlay/profiler is enabled, so it
        // costs nothing in normal play.
        m_gpuTimingActiveThisFrame = Profiler::GetInstance().IsEnabled();
        if (m_gpuTimingActiveThisFrame)
        {
            GpuTimerBegin();
        }
#endif

        // Traverse the scene graph and find all kind of lighting information for the current frame
        FindLights(scene, camera);

        if (m_shadowCastingDirectionalLight)
        {
            if (m_useCascadedShadows)
            {
                // Render cascaded shadow maps
                RenderCascadedShadowMaps(scene, camera);
            }
            else
            {
                // Render the legacy single shadow map for the directional light
                RenderShadowMap(scene, camera);
            }
        }
        else
        {
            // No shadow-casting light - ensure shadow buffer indicates no shadows
            ShadowBuffer buffer{};
            buffer.cascadeCount = 0;
            m_shadowBuffer->Update(&buffer);
        }

#ifdef _WIN32
        if (m_gpuTimingActiveThisFrame) { GpuTimerMark(1); } // after shadows
#endif

        // Render the geometry pass
        RenderGeometryPass(scene, camera);

#ifdef _WIN32
        if (m_gpuTimingActiveThisFrame) { GpuTimerMark(2); } // after G-Buffer
#endif

        // Render the lighting pass
        RenderLightingPass(scene, camera);

#ifdef _WIN32
        if (m_gpuTimingActiveThisFrame) { GpuTimerMark(3); } // after lighting
#endif

        // Forward transparency pass: render objects in the Transparent queue group and above
        // (particles, ribbon trails, etc.) on top of the lit scene.
        // Bind the final render texture (color) together with the GBuffer depth so transparent
        // objects depth-test against opaque geometry. Depth write is disabled in RenderParticles
        // via scene state — transparent objects must not corrupt the opaque depth buffer.
        // The GBuffer depth was never bound as an SRV, so there is no D3D11 resource hazard.
        // Capture the lit opaque scene into a separate texture *before* water draws over it, so
        // translucent materials can sample it for refraction. We cannot sample m_renderTexture
        // directly because the forward pass renders into it (read/write hazard).
        // m_sceneColorCopy is never Activated as a render target and is not part of the G-buffer,
        // so its deferred Resize() (which only flags m_resizePending) would otherwise never be
        // applied. Without this, after the window resizes off the constructor size the copy stays
        // at the wrong dimensions, CopyResource becomes a no-op (mismatched sizes), and t14 is left
        // as uninitialized/stale garbage — producing broken refraction (e.g. a white wash).
        m_sceneColorCopy->ApplyPendingResize();

#ifdef WIN32
        {
            GraphicsDeviceD3D11& d3dDev = static_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
            ID3D11DeviceContext& d3dCtx = d3dDev;
            auto* srcRt = static_cast<RenderTextureD3D11*>(m_renderTexture.get());
            auto* dstRt = static_cast<RenderTextureD3D11*>(m_sceneColorCopy.get());
            d3dCtx.CopyResource(dstRt->GetTex2D(), srcRt->GetTex2D());
        }
#endif

        {
            RenderTexturePtr forwardColor = m_renderTexture;
            m_device.SetRenderTargetsWithDepthStencil(&forwardColor, 1, m_gBuffer.GetDepthRTPtr());
            m_device.SetViewport(0, 0, m_gBuffer.GetWidth(), m_gBuffer.GetHeight(), 0.0f, 1.0f);

            // Expose the opaque scene's linear depth to forward/translucent materials so they can
            // sample SceneDepth (depth-based effects like shoreline softening, depth-tinted water).
            // The G-buffer normal RT stores linear view-space depth in its alpha channel (see the
            // GBuffer pass: output.normal = float4(N * 0.5 + 0.5, linearDepth)). It is not bound as
            // a render target during the forward pass, so sampling it here is hazard-free.
            // Materials declare this as Texture2D sceneDepthTex : register(t15).
            m_gBuffer.GetNormalRT().Bind(ShaderType::PixelShader, 15);

            // Expose the lit scene color copy for refraction (Texture2D sceneColorTex : register(t14)).
            m_sceneColorCopy->Bind(ShaderType::PixelShader, 14);
        }
        scene.SetForwardTransparentOnly(true);
        scene.Render(camera, PixelShaderType::Forward);
        scene.SetForwardTransparentOnly(false);

        // Release the scene SRVs so they do not collide with render targets bound in next frame.
        m_device.BindTexture(nullptr, ShaderType::PixelShader, 14);
        m_device.BindTexture(nullptr, ShaderType::PixelShader, 15);

#ifdef _WIN32
        if (m_gpuTimingActiveThisFrame)
        {
            GpuTimerMark(4); // after forward/translucent pass (end of GPU frame work)
            GpuTimerEndAndCollect();
        }
#endif
    }

    void DeferredRenderer::RenderGeometryPass(Scene& scene, Camera& camera)
    {
        if (m_depthPrepassEnabled)
        {
            // 1) Depth-only pre-pass. Renders the front-most opaque depth using the cheap ShadowMap
            //    pixel shader (which still alpha-tests foliage). This also builds the render queue
            //    for the frame; SetDepthPrepass(true) makes it capture the full visible set (not
            //    just shadow casters) so the G-Buffer pass below can reuse the same queue.
            m_gBuffer.BindDepthOnly();
            scene.SetDepthPrepass(true);
            scene.Render(camera, PixelShaderType::ShadowMap);
            scene.SetDepthPrepass(false);

            // 2) G-Buffer pass. Keep the pre-pass depth (do not clear it), reuse the queue, and let
            //    materials test LessEqual with depth-write off so occluded pixels are early-Z
            //    rejected before the expensive G-Buffer shader runs.
            m_gBuffer.Bind(false);
            m_gBuffer.GetAlbedoRT().Clear(ClearFlags::Color);
            m_gBuffer.GetNormalRT().Clear(ClearFlags::Color);
            m_gBuffer.GetEmissiveRT().Clear(ClearFlags::Color);
            m_gBuffer.GetMaterialRT().Clear(ClearFlags::Color);

            m_device.SetGBufferDepthPrepass(true);
            scene.SetReuseRenderQueue(true);
            scene.Render(camera, PixelShaderType::GBuffer);
            scene.SetReuseRenderQueue(false);
            m_device.SetGBufferDepthPrepass(false);
            return;
        }

        // Bind the G-Buffer
        m_gBuffer.Bind();

        m_gBuffer.GetAlbedoRT().Clear(ClearFlags::Color);
        m_gBuffer.GetNormalRT().Clear(ClearFlags::Color);
        m_gBuffer.GetEmissiveRT().Clear(ClearFlags::Color);
        m_gBuffer.GetMaterialRT().Clear(ClearFlags::Color);

        // Render the scene using the camera
        scene.Render(camera, PixelShaderType::GBuffer);
    }

    void DeferredRenderer::RenderLightingPass(Scene& scene, Camera& camera)
    {
        // Unbind the G-Buffer
        m_gBuffer.Unbind();

        // Set the render target to the back buffer
        m_renderTexture->Activate();

        // Disable depth testing and writing
        m_device.SetDepthEnabled(false);
        m_device.SetDepthWriteEnabled(false);

        m_device.SetTransformMatrix(World, Matrix4::Identity);
        m_device.SetTransformMatrix(View, camera.GetViewMatrix());
        m_device.SetTransformMatrix(Projection, camera.GetProjectionMatrix());
        scene.RefreshCameraBuffer(camera);

        m_gBuffer.GetAlbedoRT().Bind(ShaderType::PixelShader, 0);
        m_gBuffer.GetNormalRT().Bind(ShaderType::PixelShader, 1);
        m_gBuffer.GetMaterialRT().Bind(ShaderType::PixelShader, 2);
        m_gBuffer.GetEmissiveRT().Bind(ShaderType::PixelShader, 3);
        // Slot 4 (former ViewRay G-Buffer) is no longer bound — the view ray is reconstructed
        // analytically in the lighting shader from InverseProjection and the screen UV.

        // Bind all cascade shadow maps
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            m_cascadeShadowMaps[i]->Bind(ShaderType::PixelShader, 5 + i);
        }

        // Set the vertex format for the full-screen quad
        m_device.SetVertexFormat(VertexFormat::PosColorTex1);

        // Bind the G-Buffer textures to the lighting material
        m_deferredLightVs->Set();
        m_deferredLightPs->Set();

        // Set the topology type for the full-screen quad
        m_device.SetTopologyType(TopologyType::TriangleList);

        // Render a full screen quad
    	m_quadBuffer->Set(0);

        // Bind buffer to stage
        scene.GetCameraBuffer()->BindToStage(ShaderType::PixelShader, 1);
        m_lightMetadataBuffer->BindToStage(ShaderType::PixelShader, 2);
        m_shadowBuffer->BindToStage(ShaderType::PixelShader, 3);
        
        // Bind the structured buffer for lights (t9 slot in shader)
        m_lightStructuredBuffer->BindToStage(ShaderType::PixelShader, 9);

        m_device.SetFillMode(FillMode::Solid);
        m_device.SetFaceCullMode(FaceCullMode::None);
        m_device.SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
        m_device.SetTextureFilter(TextureFilter::Trilinear);

#ifdef WIN32
        GraphicsDeviceD3D11& d3ddev = (GraphicsDeviceD3D11&)GraphicsDevice::Get();
        ID3D11DeviceContext& d3d11ctx = d3ddev;
		ID3D11SamplerState* samplers[1] = { m_shadowSampler.Get() };
        d3d11ctx.PSSetSamplers(1, 1, samplers);
#endif
        
        // Draw a full-screen quad
        m_device.Draw(6);

        m_renderTexture->Update();
    }

    void DeferredRenderer::FindLights(Scene& scene, Camera& camera)
    {
        m_shadowCastingDirectionalLight = nullptr;

        // Use the scene's efficient light gathering with frustum culling and priority sorting
        const auto visibleLights = scene.GatherVisibleLights(camera, MAX_LIGHTS);

        // Convert visible lights to shader format
        std::vector<ShaderLight> shaderLights;
        shaderLights.reserve(visibleLights.size());

        for (const auto& visibleLight : visibleLights)
        {
            ShaderLight shaderLight;
            shaderLight.position = visibleLight.position;
            shaderLight.color = visibleLight.color;
            shaderLight.intensity = visibleLight.intensity;
            shaderLight.range = visibleLight.range;
            shaderLight.direction = visibleLight.direction;
            shaderLight.spotAngle = visibleLight.spotAngle;
            shaderLight.shadowMap = visibleLight.castsShadows ? 1 : 0;
            shaderLight.padding = Vector2::Zero;

            // Set light type
            switch (visibleLight.type)
            {
            case LightType::Directional:
                shaderLight.type = 1;
                // Track first shadow-casting directional light
                if (m_shadowCastingDirectionalLight == nullptr && visibleLight.castsShadows)
                {
                    m_shadowCastingDirectionalLight = visibleLight.light;
                }
                break;
            case LightType::Point:
                shaderLight.type = 0;
                break;
            case LightType::Spot:
                shaderLight.type = 2;
                break;
            }

            shaderLights.push_back(shaderLight);
        }

        // Update the light metadata constant buffer
        LightMetadata metadata;
        metadata.ambientColor = scene.GetAmbientColor();
        metadata.lightCount = static_cast<uint32>(shaderLights.size());
        m_lightMetadataBuffer->Update(&metadata);

        // Update the structured buffer with light data
        if (!shaderLights.empty())
        {
            m_lightStructuredBuffer->Update(shaderLights.data(), shaderLights.size());
        }

        // Register the shadow-casting directional light as the scene's primary light so that
        // forward-rendered translucent objects (water, particles …) use the real sun direction
        // and colour instead of the previously hardcoded fallback values.
        scene.SetPrimaryDirectionalLight(m_shadowCastingDirectionalLight);

        // Store statistics for external access
        m_lastLightStats = scene.GetLightRenderStats();
    }

    void DeferredRenderer::RenderShadowMap(Scene& scene, Camera& camera)
    {
        if (m_shadowCastingDirectionalLight == nullptr)
        {
            return;
        }

        m_shadowCameraSetup->SetupShadowCamera(scene, camera, *m_shadowCastingDirectionalLight, *m_shadowCameras[0]);

        // Setup some depth bias settings
        m_device.SetDepthBias(m_depthBias);
        m_device.SetSlopeScaledDepthBias(m_slopeScaledDepthBias);
        m_device.SetDepthBiasClamp(m_depthBiasClamp);
        
        // Update the shadow buffer with legacy single cascade data
        ShadowBuffer buffer{};
        buffer.cascadeViewProjections[0] = (m_shadowCameras[0]->GetProjectionMatrix() * m_shadowCameras[0]->GetViewMatrix());
        buffer.cascadeSplitDistances = Vector4(camera.GetFarClipDistance(), 0.0f, 0.0f, 0.0f);
        buffer.shadowBias = m_shadowBias;
        buffer.normalBiasScale = m_normalBiasScale;
        buffer.shadowSoftness = m_shadowSoftness;
        buffer.blockerSearchRadius = m_blockerSearchRadius;
        buffer.lightSize = m_lightSize;
        buffer.cascadeCount = 1;
        buffer.debugCascades = m_debugCascades ? 1 : 0;
        buffer.cascadeBlendFactor = 0.0f;
        buffer.pcfSampleCount = m_pcfSampleCount;
        m_shadowBuffer->Update(&buffer);

        // Render the shadow map
        m_cascadeShadowMaps[0]->Activate();
        m_cascadeShadowMaps[0]->Clear(ClearFlags::Depth);
        scene.Render(*m_shadowCameras[0], PixelShaderType::ShadowMap);
        m_cascadeShadowMaps[0]->Update();

        // Reset
        m_device.SetDepthBias(0);
        m_device.SetSlopeScaledDepthBias(0);
        m_device.SetDepthBiasClamp(0);
    }

    void DeferredRenderer::RenderCascadedShadowMaps(Scene& scene, Camera& camera)
    {
        if (m_shadowCastingDirectionalLight == nullptr)
        {
            return;
        }

        // Setup all cascades
        m_cascadedShadowSetup->SetupCascades(scene, camera, *m_shadowCastingDirectionalLight, m_shadowCameras);

        // Get cascade data
        const auto& cascades = m_cascadedShadowSetup->GetCascades();
        const auto& config = m_cascadedShadowSetup->GetConfig();

        const uint32 activeCascades = m_cascadedShadowSetup->GetConfig().GetActiveCascadeCount();

        // Cull shadow casters ONCE for the whole shadow region, then reuse the list for every cascade.
        // The gather region is the union of all active cascade frusta in world space, so it provably
        // contains every caster any cascade could draw; each cascade below re-filters this list with
        // its own (tight) frustum, so the rendered set — and therefore the shadows — are unchanged.
        // This replaces N full octree walks (one per cascade) with a single walk plus N cheap linear
        // filters over a much smaller list.
        AABB gatherRegion;
        gatherRegion.SetNull();
        for (uint32 i = 0; i < activeCascades; ++i)
        {
            if (!m_shadowCameras[i])
            {
                continue;
            }

            const Vector3* corners = m_shadowCameras[i]->GetWorldSpaceCorners();
            for (int c = 0; c < 8; ++c)
            {
                gatherRegion.Combine(corners[c]);
            }
        }

        scene.GatherShadowCasters(gatherRegion, m_shadowCasterCache);

        // Setup depth bias
        m_device.SetDepthBias(m_depthBias);
        m_device.SetSlopeScaledDepthBias(m_slopeScaledDepthBias);
        m_device.SetDepthBiasClamp(m_depthBiasClamp);

        // Render each active cascade by re-filtering the gathered caster list against the cascade's
        // own frustum. Each cascade still draws into its own shadow map, but no longer re-walks the
        // octree or re-runs full per-object visibility from scratch.
        for (uint32 i = 0; i < activeCascades; ++i)
        {
            if (!m_shadowCameras[i])
            {
                continue;
            }

            m_cascadeShadowMaps[i]->Activate();
            m_cascadeShadowMaps[i]->Clear(ClearFlags::Depth);

            // Sub-texel small-object culling: a caster whose full world extent is below one shadow
            // texel casts a shadow under a single texel — blurred away entirely by the 16-tap PCF —
            // so skipping it has no visible effect. We compare the world half-extent against half a
            // texel (full extent < 1 texel) to stay strictly on the lossless side. worldTexelSize
            // grows with cascade distance, so distant cascades naturally skip more tiny casters while
            // the near cascade keeps virtually everything.
            const float minCasterWorldRadius = cascades[i].worldTexelSize * 0.5f;
            scene.RenderShadowCasters(*m_shadowCameras[i], m_shadowCasterCache, minCasterWorldRadius);

            m_cascadeShadowMaps[i]->Update();
        }

        // Update the shadow buffer with cascade data
        // Use the actual shadow camera matrices (what we rendered with) instead of computed ones
        ShadowBuffer buffer{};
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            // Use the shadow camera's actual view-projection matrix
            buffer.cascadeViewProjections[i] = m_shadowCameras[i]->GetProjectionMatrix() * m_shadowCameras[i]->GetViewMatrix();
        }
        buffer.cascadeSplitDistances = Vector4(
            cascades[0].splitDistance,
            cascades[1].splitDistance,
            cascades[2].splitDistance,
            cascades[3].splitDistance
        );
        buffer.shadowBias = m_shadowBias;
        buffer.normalBiasScale = m_normalBiasScale;
        buffer.shadowSoftness = m_shadowSoftness;
        buffer.blockerSearchRadius = m_blockerSearchRadius;
        buffer.lightSize = m_lightSize;
        buffer.cascadeCount = activeCascades;
        buffer.debugCascades = m_debugCascades ? 1 : 0;
        buffer.cascadeBlendFactor = config.cascadeBlendFactor;
        buffer.pcfSampleCount = m_pcfSampleCount;
        m_shadowBuffer->Update(&buffer);

        // Reset depth bias
        m_device.SetDepthBias(0);
        m_device.SetSlopeScaledDepthBias(0);
        m_device.SetDepthBiasClamp(0);
    }

    TexturePtr DeferredRenderer::GetFinalRenderTarget() const
    {
        return m_renderTexture;
    }

    void DeferredRenderer::SetShadowMapSize(const uint16 size)
    {
        if (m_shadowMapSize == size || size == 0)
        {
            return;
        }

        m_shadowMapSize = size;
        
        // Resize all cascade shadow maps
        for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
        {
            if (m_cascadeShadowMaps[i])
            {
                m_cascadeShadowMaps[i]->Resize(m_shadowMapSize, m_shadowMapSize);
                m_cascadeShadowMaps[i]->ApplyPendingResize();
            }
        }
        
        // Update CSM config
        if (m_cascadedShadowSetup)
        {
            m_cascadedShadowSetup->GetConfig().shadowMapSize = size;
        }
    }
}
