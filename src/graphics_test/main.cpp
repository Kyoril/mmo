#ifdef _WIN32
#	include<Windows.h>
#else
#endif

#include "graphics/graphics_device.h"
#include "event_loop.h"
#include "assets/asset_registry.h"
#include "scene_graph/scene.h"
#include "scene_graph/octree_scene.h"
#include "deferred_shading/deferred_renderer.h"
#include "log/default_log.h"
#include "log/default_log_levels.h"
#include "log/log_entry.h"
#include "base/profiler.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/ribbon_trail.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

#include "scene_graph/foliage.h"
#include "scene_graph/mesh_manager.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#	include <d3d11.h>
#	include "graphics_d3d11/graphics_device_d3d11.h"
#endif

namespace mmo
{
	static bool g_lmb = false;
	static int32 lastX = -1;
	static int32 lastY = -1;

	std::unique_ptr<Scene> g_scene;
	Camera* g_camera = nullptr;
	SceneNode* g_cameraNode = nullptr;

	SceneNode* g_boarNode = nullptr;
	Entity* g_boarEntity = nullptr;

	SceneNode* g_particleNode = nullptr;
	ParticleEmitter* g_particleEmitter = nullptr;

	SceneNode* g_ribbonTrailNode = nullptr;
	RibbonTrail* g_ribbonTrail = nullptr;
	float g_ribbonOrbitAngle = 0.0f;

	SceneNode* g_sunLightNode = nullptr;
	Light* g_sunLight = nullptr;
	SceneNode* g_pointLightRotator = nullptr;
	SceneNode* g_pointLightNode = nullptr;
	Light* g_pointLight = nullptr;

	Entity* g_lightDebugEnt = nullptr;

	SceneNode* g_pointLight2Node = nullptr;
	Light* g_pointLight2 = nullptr;

	std::unique_ptr<terrain::Terrain> g_terrain;

	AnimationState* g_idleState = nullptr;

	std::unique_ptr<DeferredRenderer> g_deferredRenderer = nullptr;

	VertexBufferPtr g_quadBuffer = nullptr;

	std::unique_ptr<Foliage> g_foliage = nullptr;

	/// Scripted volumetric fog reproduction (`--fog-repro`): a low sun shining through a row of pillars
	/// into the fog toward a camera that follows a fixed path at a fixed step per frame. Every captured
	/// frame is written as raw RGBA8 to the output directory, so fog motion can be measured offline
	/// against exactly known camera motion.
	struct FogReproOptions
	{
		bool enabled = false;
		int32 debugMode = 0;
		float history = 0.9f;
		int32 quality = 3;
		int32 tileSize = 0;			// > 0 overrides the preset grid (ground-truth renders)
		int32 sliceCount = 0;
		int32 poses = 120;			// camera poses along the path
		int32 settle = 1;			// frames rendered per pose; > 1 lets the fog converge (reference renders)
		float step = 0.1f;			// metres (or degrees for yaw) the camera moves per pose
		std::string motion = "strafe";
		std::string outDir = "fog_repro";
		bool jitter = true;
		float anisotropy = 0.7f;
		bool perf = false;			// average the GPU pass timings into <out>/perf.txt (use a Release build)
		std::string scene = "pillars";	// pillars | canopy (a tree crown of small blocks with many small gaps)
		int32 sunTapsXY = -1;		// -1 keeps the quality preset's sun shadow taps
		int32 sunTapsZ = -1;
		bool fog = true;			// --nofog: judge the surface shadows alone
		int32 shadowQuality = -1;	// -1 keeps the renderer default (High)
		int32 shadowMapSize = 0;	// 0 keeps the renderer default
	};

	static FogReproOptions g_repro;
	static int32 g_reproFrame = 0;
	static std::vector<SceneNode*> g_reproNodes;
	static std::vector<Entity*> g_reproEntities;
	static std::map<std::string, std::pair<double, int32>> g_reproPerf;

	namespace
	{
		FogReproOptions ParseFogReproOptions(const std::string& commandLine)
		{
			FogReproOptions options;
			std::istringstream stream(commandLine);
			std::string token;
			while (stream >> token)
			{
				const auto value = [&token](const char* key) -> const char*
				{
					const size_t length = std::strlen(key);
					return token.compare(0, length, key) == 0 ? token.c_str() + length : nullptr;
				};

				if (token == "--fog-repro") { options.enabled = true; }
				else if (const char* v = value("--debug=")) { options.debugMode = std::atoi(v); }
				else if (const char* v = value("--history=")) { options.history = static_cast<float>(std::atof(v)); }
				else if (const char* v = value("--quality=")) { options.quality = std::atoi(v); }
				else if (const char* v = value("--tile=")) { options.tileSize = std::atoi(v); }
				else if (const char* v = value("--slices=")) { options.sliceCount = std::atoi(v); }
				else if (const char* v = value("--poses=")) { options.poses = std::atoi(v); }
				else if (const char* v = value("--settle=")) { options.settle = std::max(1, std::atoi(v)); }
				else if (const char* v = value("--step=")) { options.step = static_cast<float>(std::atof(v)); }
				else if (const char* v = value("--motion=")) { options.motion = v; }
				else if (const char* v = value("--out=")) { options.outDir = v; }
				else if (token == "--nojitter") { options.jitter = false; }
				else if (token == "--perf") { options.perf = true; }
				else if (token == "--nofog") { options.fog = false; }
				else if (const char* v = value("--shadowq=")) { options.shadowQuality = std::atoi(v); }
				else if (const char* v = value("--shadowsize=")) { options.shadowMapSize = std::atoi(v); }
				else if (const char* v = value("--scene=")) { options.scene = v; }
				else if (const char* v = value("--suntaps=")) { options.sunTapsXY = std::atoi(v); }
				else if (const char* v = value("--suntapsz=")) { options.sunTapsZ = std::atoi(v); }
				else if (const char* v = value("--aniso=")) { options.anisotropy = static_cast<float>(std::atof(v)); }
			}

			return options;
		}

		/// Camera pose for a pose index along the scripted path.
		void ApplyReproPose(const int32 pose)
		{
			const float t = static_cast<float>(pose) * g_repro.step;
			Vector3 position(-6.0f, 2.5f, 25.0f);
			Radian yaw(0.0f);
			if (g_repro.motion == "strafe")
			{
				position.x += t;
			}
			else if (g_repro.motion == "forward")
			{
				position.z -= t;
			}
			else if (g_repro.motion == "yaw")
			{
				yaw = Degree(t);
			}
			else if (g_repro.motion == "orbit")
			{
				// Third-person camera: orbit a pivot 6 m in front of the start position, like the client's
				// camera anchor turning around the character.
				yaw = Degree(t);
				const Vector3 pivot = position + Vector3(0.0f, 0.0f, -6.0f);
				position = pivot + Quaternion(yaw, Vector3::UnitY) * Vector3(0.0f, 0.0f, 6.0f);
			}

			g_cameraNode->SetPosition(position);
			g_cameraNode->SetOrientation(Quaternion(yaw, Vector3::UnitY) * Quaternion(Degree(4.0f), Vector3::UnitX));
		}

		void CaptureFinalTarget(const int32 index)
		{
#ifdef _WIN32
			const TexturePtr target = g_deferredRenderer->GetFinalRenderTarget();
			auto* source = static_cast<ID3D11Texture2D*>(target ? target->GetRawTexture() : nullptr);
			if (!source)
			{
				ELOG("fog repro: final render target has no D3D11 texture");
				return;
			}

			auto& device = static_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
			ID3D11Device& d3dDevice = device;
			ID3D11DeviceContext& context = device;

			D3D11_TEXTURE2D_DESC desc;
			source->GetDesc(&desc);
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			desc.MiscFlags = 0;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.SampleDesc.Count = 1;

			ID3D11Texture2D* staging = nullptr;
			if (FAILED(d3dDevice.CreateTexture2D(&desc, nullptr, &staging)))
			{
				ELOG("fog repro: could not create staging texture");
				return;
			}

			context.CopyResource(staging, source);

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(context.Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
			{
				if (index == 0)
				{
					std::ofstream meta(g_repro.outDir + "/meta.txt");
					meta << desc.Width << " " << desc.Height << " " << static_cast<int>(desc.Format) << "\n";
				}

				char name[64];
				std::snprintf(name, sizeof(name), "/%04d.bin", index);
				std::ofstream file(g_repro.outDir + name, std::ios::binary);
				const uint32 bytesPerPixel = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
				for (UINT y = 0; y < desc.Height; ++y)
				{
					file.write(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, desc.Width * bytesPerPixel);
				}
				context.Unmap(staging, 0);
			}

			staging->Release();
#endif
		}

		void SetupFogRepro()
		{
			// Keep only what the fog needs: no animated or light-emitting props that change over time.
			g_particleEmitter->Stop();
			g_ribbonTrail->Stop();
			g_pointLight->SetVisible(false);
			g_pointLight2->SetVisible(false);
			g_lightDebugEnt->SetVisible(false);
			g_boarEntity->SetVisible(false);

			// Low sun straight ahead of the camera, so the pillars' shadows run toward it as shafts.
			g_sunLight->SetDirection(Vector3(0.12f, -0.22f, 1.0f).NormalizedCopy());

			AtmosphereParameters atmosphere;
			atmosphere.SetDensity(0.006f);
			atmosphere.SetHeightFalloff(0.05f);
			atmosphere.SetBaseHeight(0.0f);
			atmosphere.SetAnisotropy(g_repro.anisotropy);
			atmosphere.SetShaftStrength(1.25f);
			g_scene->SetAtmosphereParameters(atmosphere);
			g_scene->SetFogEnabled(g_repro.fog);

			// A row of tall pillars with gaps (the shaft casters) and a wall far behind them (a sharp
			// geometric reference at a known depth).
			const auto addBox = [](const Vector3& position, const Vector3& size)
			{
				Entity* entity = g_scene->CreateEntity("ReproBox" + std::to_string(g_reproEntities.size()), "Models/Cube/Cube.hmsh");
				ASSERT(entity);
				const AABB& bounds = entity->GetMesh()->GetBounds();
				const Vector3 extent = bounds.max - bounds.min;
				SceneNode* node = g_scene->GetRootSceneNode().CreateChildSceneNode();
				node->AttachObject(*entity);
				node->SetScale(Vector3(size.x / extent.x, size.y / extent.y, size.z / extent.z));
				node->SetPosition(position - (bounds.min + extent * 0.5f) * Vector3(size.x / extent.x, size.y / extent.y, size.z / extent.z) + Vector3(0.0f, size.y * 0.5f, 0.0f));
				g_reproEntities.push_back(entity);
				g_reproNodes.push_back(node);
			};

			if (g_repro.scene == "canopy")
			{
				// A tree crown: small blocks scattered through an ellipsoid on a trunk, leaving many gaps
				// a fraction of a froxel wide, like the foliage the fog shafts come through in the client.
				uint32 state = 12345u;
				const auto random = [&state]()
				{
					state = state * 1664525u + 1013904223u;
					return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
				};
				addBox(Vector3(-12.0f, 0.0f, -30.0f), Vector3(1.2f, 11.0f, 1.2f));
				for (int32 i = 0; i < 700; ++i)
				{
					Vector3 offset(random() * 2.0f - 1.0f, random() * 2.0f - 1.0f, random() * 2.0f - 1.0f);
					if (offset.GetSquaredLength() > 1.0f)
					{
						continue;
					}
					addBox(Vector3(-12.0f + offset.x * 9.0f, 13.0f + offset.y * 4.5f, -30.0f + offset.z * 5.0f), Vector3(0.7f, 0.5f, 0.7f));
				}
				addBox(Vector3(0.0f, 0.0f, -60.0f), Vector3(120.0f, 6.0f, 1.0f));
			}
			else
			{
				for (int32 i = -12; i <= 12; ++i)
				{
					addBox(Vector3(static_cast<float>(i) * 4.0f, 0.0f, -35.0f), Vector3(1.5f, 16.0f, 1.5f));
				}
				addBox(Vector3(0.0f, 0.0f, -38.0f), Vector3(120.0f, 1.0f, 1.0f));
				addBox(Vector3(0.0f, 16.0f, -35.0f), Vector3(120.0f, 1.0f, 3.0f));
			}

			g_deferredRenderer->SetAtmosphereQuality(g_repro.quality);
			if (g_repro.shadowQuality >= 0)
			{
				g_deferredRenderer->SetShadowQuality(g_repro.shadowQuality);
			}
			if (g_repro.shadowMapSize > 0)
			{
				g_deferredRenderer->SetShadowMapSize(static_cast<uint16>(g_repro.shadowMapSize));
			}
			VolumetricFogSettings& fog = g_deferredRenderer->GetVolumetricFogSettings();
			if (g_repro.tileSize > 0)
			{
				fog.tileSize = static_cast<uint32>(g_repro.tileSize);
			}
			if (g_repro.sliceCount > 0)
			{
				fog.sliceCount = static_cast<uint32>(g_repro.sliceCount);
			}
			fog.SetHistoryWeight(g_repro.history);
			fog.jitterEnabled = g_repro.jitter;
			fog.SetSunTaps(g_repro.sunTapsXY >= 0 ? static_cast<uint32>(g_repro.sunTapsXY) : fog.sunTapsXY,
				g_repro.sunTapsZ >= 0 ? static_cast<uint32>(g_repro.sunTapsZ) : fog.sunTapsZ);
			fog.debugMode = static_cast<uint32>(g_repro.debugMode);
			fog.SetRange(200.0f);

			std::filesystem::create_directories(g_repro.outDir);
			if (g_repro.perf)
			{
				Profiler::GetInstance().SetEnabled(true);
			}
			ApplyReproPose(0);
		}
	}

	void RenderScene()
	{
		GraphicsDevice::Get().Reset();

		if (g_repro.perf)
		{
			Profiler::GetInstance().BeginFrame();
		}

		// Use deferred renderer to render the scene
		g_deferredRenderer->Render(*g_scene, *g_camera);

		if (g_repro.perf)
		{
			Profiler::GetInstance().EndFrame();

			// GPU timestamps arrive a few frames late; skip the warm-up.
			if (g_reproFrame >= 30)
			{
				for (const PerformanceMetric& metric : Profiler::GetInstance().GetMetrics())
				{
					if (metric.name.rfind("GPU: ", 0) == 0)
					{
						auto& [total, count] = g_reproPerf[metric.name];
						total += metric.totalTimeMs;
						++count;
					}
				}
			}
		}

		if (g_repro.enabled)
		{
			// Capture the last frame of every pose, then move on; exit after the last pose.
			const int32 pose = g_reproFrame / g_repro.settle;
			if (g_reproFrame % g_repro.settle == g_repro.settle - 1)
			{
				CaptureFinalTarget(pose);
				if (pose + 1 >= g_repro.poses)
				{
					if (g_repro.perf)
					{
						std::ofstream perf(g_repro.outDir + "/perf.txt");
						for (const auto& [name, entry] : g_reproPerf)
						{
							perf << name << ": " << (entry.first / std::max(entry.second, 1)) << " ms (" << entry.second << " frames)\n";
						}
					}

					EventLoop::Terminate(0);
				}
			}

			++g_reproFrame;
			ApplyReproPose(g_reproFrame / g_repro.settle);
		}

		GraphicsDevice::Get().GetAutoCreatedWindow()->Activate();

		// Bind render target to texture stage
		g_deferredRenderer->GetFinalRenderTarget()->Bind(ShaderType::PixelShader, 0);
		GraphicsDevice::Get().SetTextureAddressMode(TextureAddressMode::Clamp, TextureAddressMode::Clamp, TextureAddressMode::Clamp);
		GraphicsDevice::Get().SetTextureFilter(TextureFilter::None);
		GraphicsDevice::Get().SetFillMode(FillMode::Solid);
		GraphicsDevice::Get().SetFaceCullMode(FaceCullMode::Back);
		GraphicsDevice::Get().SetVertexFormat(VertexFormat::PosColorTex1);
		GraphicsDevice::Get().SetTopologyType(TopologyType::TriangleList);
		g_quadBuffer->Set(0);

		// Draw a full-screen quad
		GraphicsDevice::Get().Draw(6);
	}

	void OnIdle(float elapsedTime, GameTime time)
	{
		if (g_repro.enabled)
		{
			// Deterministic: nothing but the scripted camera may change between frames.
			return;
		}

		// Update scene objects and stuff
		if (g_pointLightRotator)
		{
			g_pointLightRotator->Yaw(Radian(elapsedTime), TransformSpace::World);
		}

		if (g_pointLight2Node)
		{
			g_pointLight2Node->SetPosition(Vector3::UnitY * 3.0f + Vector3::UnitY * 2.0f * ::sin(static_cast<double>(time) / 1000.0));
		}

		if (g_idleState)
		{
			g_idleState->AddTime(elapsedTime);
		}

		if (g_foliage)
		{
			g_foliage->Update(*g_camera);
		}

		// Update ribbon trail orbit position (simulates a weapon swing or orbiting projectile)
		if (g_ribbonTrailNode)
		{
			g_ribbonOrbitAngle += elapsedTime * 3.0f;  // Rotate at 3 radians per second
			const float orbitRadius = 4.0f;
			const float height = 3.0f + ::sin(g_ribbonOrbitAngle * 0.5f) * 1.5f;
			g_ribbonTrailNode->SetPosition(Vector3(
				::cos(g_ribbonOrbitAngle) * orbitRadius,
				height,
				::sin(g_ribbonOrbitAngle) * orbitRadius
			));
		}
	}

	void OnPaint()
	{
		// Since our deferred renderer renders directly to the back buffer,
		// we don't need to do any additional rendering here
		RenderScene();
	}

    scoped_connection_container signals;

	bool InitializeGlobal()
	{
#ifdef _WIN32
		g_repro = ParseFogReproOptions(GetCommandLineA());
#endif

        // Describe our graphics device
        GraphicsDeviceDesc desc {};
        desc.width = 1920;
        desc.height = 1080;
        desc.windowed = true;
        desc.vsync = true;
#ifdef _WIN32
        GraphicsDevice::CreateD3D11(desc);
#elif defined(__APPLE__)
        GraphicsDevice::CreateMetal(desc);
#endif

        const RenderWindowPtr window = GraphicsDevice::Get().GetAutoCreatedWindow();
        ASSERT(window);

        // Shutdown event loop when window is closed
        window->SetTitle("MMO Graphics Test");
        signals += window->Closed.connect([]() { EventLoop::Terminate(0); });

        // Initialize asset registry in working directory
        AssetRegistry::Initialize(".", {});

        // Setup event loop
        EventLoop::Initialize();
        signals += EventLoop::Idle.connect(OnIdle);
        signals += EventLoop::Paint.connect(OnPaint);

        // Terminate application when escape was pressed
        signals += EventLoop::KeyDown.connect([](const int32 key, bool repeat)
        {
	        if (key == 0x1B)
	        {
		        EventLoop::Terminate(0);
	        }

			if (key == 'V')
			{
				// Toggle directional light
				g_sunLight->SetVisible(!g_sunLight->IsVisible());
			}

			if (key == 'R')
			{
				// Toggle directional light
				g_pointLight->SetVisible(!g_pointLight->IsVisible());
				g_lightDebugEnt->SetVisible(g_pointLight->IsVisible());
			}
			if (key == 'G')
			{
				// Toggle directional light
				g_pointLight2->SetVisible(!g_pointLight2->IsVisible());
			}

			if (key == 'P')
			{
				// Toggle directional light
				g_camera->SetProjectionType(ProjectionType::Perspective);
			}
			if (key == 'O')
			{
				g_camera->SetProjectionType(ProjectionType::Orthographic);
				g_camera->SetOrthoWindow(1920.0f * 0.5f, 1080.0f * 0.5f);
			}

			if (key == 'T')
			{
				// Toggle particle emitter
				if (g_particleEmitter)
				{
					if (g_particleEmitter->IsPlaying())
					{
						g_particleEmitter->Stop();
					}
					else
					{
						g_particleEmitter->Play();
					}
				}
			}

			if (key == 'Y')
			{
				// Toggle ribbon trail
				if (g_ribbonTrail)
				{
					if (g_ribbonTrail->IsPlaying())
					{
						g_ribbonTrail->Stop();
					}
					else
					{
						g_ribbonTrail->Reset();
						g_ribbonTrail->Play();
					}
				}
			}

        	return true;
        });

		signals += EventLoop::MouseDown.connect([](EMouseButton button, int32 x, int32 y)
			{
				lastX = x;
				lastY = y;

				if (button == MouseButton_Left)
				{
					g_lmb = true;
				}
				return true;
			});


		signals += EventLoop::MouseUp.connect([](EMouseButton button, int32 x, int32 y)
			{
				lastX = x;
				lastY = y;

				if (button == MouseButton_Left)
				{
					g_lmb = false;
				}
				return true;
			});

		signals += EventLoop::MouseMove.connect([](int32 x, int32 y)
			{
				if (g_lmb)
				{
					if (g_cameraNode)
					{
						g_cameraNode->Yaw(Radian(-static_cast<float>(x - lastX) * 0.01f), TransformSpace::Parent);
						g_cameraNode->Pitch(Radian(-static_cast<float>(y - lastY) * 0.01f), TransformSpace::Local);
					}
				}

				lastX = x;
				lastY = y;
				return true;
			});

		// Setup scene
		g_scene = std::make_unique<OctreeScene>();

		// Create a camera
		g_camera = g_scene->CreateCamera("MainCamera");
		g_camera->SetNearClipDistance(0.3f);
		g_camera->SetFarClipDistance(1000.0f);
		g_cameraNode = g_scene->GetRootSceneNode().CreateChildSceneNode("MainCameraNode");
		g_cameraNode->AttachObject(*g_camera);
		g_cameraNode->SetPosition(Vector3(0.0f, 8.5f, 15.0f));
		g_cameraNode->LookAt(Vector3::Zero, TransformSpace::World, Vector3::NegativeUnitZ);
		g_camera->SetAspectRatio(1920.0f / 1080.0f);

		g_boarNode = g_scene->GetRootSceneNode().CreateChildSceneNode("BoarNode");
		ASSERT(g_boarNode);
		g_boarEntity = g_scene->CreateEntity("Boar", "Models/Creatures/Boar/Boar.hmsh");	// Models/Creatures/Boar/Boar.hmsh
		ASSERT(g_boarEntity);
		g_boarNode->AttachObject(*g_boarEntity);

		if (g_boarEntity->HasAnimationState("Idle"))
		{
			g_idleState = g_boarEntity->GetAnimationState("Idle");
			g_idleState->SetEnabled(true);
			g_idleState->SetLoop(true);
		}

		// Create a directional light for the scene
		g_sunLight = &g_scene->CreateLight("MainLight", LightType::Directional);
		g_sunLight->SetColor(Vector4(1.0f, 0.95f, 0.8f, 1.0f)); // Warm sunlight color
		g_sunLight->SetIntensity(1.0f);
		g_sunLight->SetCastShadows(true);
		g_sunLight->SetShadowFarDistance(75.0f);
		// Use a less vertical light direction for better shadow definition
		g_sunLight->SetDirection({ -0.7f, -0.7f, -0.3f });

		g_sunLightNode = g_scene->GetRootSceneNode().CreateChildSceneNode("SunLightNode");
		g_sunLightNode->AttachObject(*g_sunLight);
		
		g_pointLightRotator = g_scene->GetRootSceneNode().CreateChildSceneNode("PointLightRotator", Vector3::UnitY * 1.5f);

		g_pointLight = &g_scene->CreateLight("PointLight", LightType::Point);
		g_pointLight->SetColor(Vector4(1.0f, 0.0f, 0.0f, 1.0f)); // Red point light
		g_pointLight->SetIntensity(1.0f);
		g_pointLight->SetRange(15.0f);
		g_pointLightNode = g_pointLightRotator->CreateChildSceneNode("PointLightNode", Vector3(0.0f, 2.0f, 10.0f));
		g_pointLightNode->AttachObject(*g_pointLight);
		g_pointLightNode->SetScale(Vector3::UnitScale * 0.5f);

		g_pointLight2 = &g_scene->CreateLight("PointLight2", LightType::Point);
		g_pointLight2->SetColor(Vector4(0.0f, 1.0f, 0.0f, 1.0f)); // Green point light
		g_pointLight2->SetIntensity(1.0f);
		g_pointLight2->SetRange(15.0f);
		g_pointLight2Node = g_scene->GetRootSceneNode().CreateChildSceneNode()->CreateChildSceneNode("PointLightNode2", Vector3::UnitY * 3.0f);
		g_pointLight2Node->AttachObject(*g_pointLight2);
		g_pointLight2Node->SetScale(Vector3::UnitScale * 0.5f);

		//g_sunLight->SetVisible(false);
		//g_pointLight2->SetVisible(false);

		g_lightDebugEnt = g_scene->CreateEntity("LightDebug", "Editor/Joint.hmsh");
		ASSERT(g_lightDebugEnt);
		g_pointLightNode->AttachObject(*g_lightDebugEnt);

		// Create a sample particle emitter
		g_particleEmitter = g_scene->CreateParticleEmitter("TestParticles");
		g_particleNode = g_scene->GetRootSceneNode().CreateChildSceneNode("ParticleNode", Vector3(5.0f, 3.0f, 0.0f));
		g_particleNode->AttachObject(*g_particleEmitter);

		// Configure particle emitter parameters
		ParticleEmitterParameters params;
		params.spawnRate = 20.0f;              // 20 particles per second
		params.maxParticles = 100;             // Max 100 particles alive at once
		params.minLifetime = 2.0f;             // Particles live 2-4 seconds
		params.maxLifetime = 4.0f;
		params.minStartSize = 0.5f;            // Start at 0.5 units
		params.maxStartSize = 0.5f;
		params.sizeOverLife = FloatCurve(1.0f, 0.2f); // Fade to 0.1 units (0.5 * 0.2)
		params.minVelocity = Vector3(-1.0f, 2.0f, -1.0f);  // Upward fountain
		params.maxVelocity = Vector3(1.0f, 4.0f, 1.0f);
		params.gravity = Vector3(0.0f, -2.0f, 0.0f);       // Gravity pulls down
		params.shape = EmitterShape::Sphere;               // Spawn in sphere
		params.shapeExtents = Vector3(0.5f, 0.5f, 0.5f);   // Radius 0.5

		// Create a color curve: white -> yellow -> orange -> transparent
		params.colorOverLifetime.Clear();
		params.colorOverLifetime.AddKey(0.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f));  // White
		params.colorOverLifetime.AddKey(0.3f, Vector4(1.0f, 1.0f, 0.5f, 1.0f));  // Light yellow
		params.colorOverLifetime.AddKey(0.6f, Vector4(1.0f, 0.6f, 0.2f, 0.8f));  // Orange
		params.colorOverLifetime.AddKey(1.0f, Vector4(0.8f, 0.3f, 0.1f, 0.0f));  // Dark orange, transparent

		g_particleEmitter->SetParameters(params);
		
		// Try to load a default material (will fall back gracefully if not found)
		MaterialPtr particleMaterial = ParticleEmitter::GetDefaultMaterial(true); // Additive blending
		if (particleMaterial)
		{
			g_particleEmitter->SetMaterial(particleMaterial);
		}

		// Start playing automatically
		g_particleEmitter->Play();

		// ==================== Ribbon Trail Demo ====================
		// Create a ribbon trail that will orbit around the scene
		g_ribbonTrail = g_scene->CreateRibbonTrail("TestRibbonTrail");
		g_ribbonTrailNode = g_scene->GetRootSceneNode().CreateChildSceneNode("RibbonTrailNode", Vector3(4.0f, 3.0f, 0.0f));
		g_ribbonTrailNode->AttachObject(*g_ribbonTrail);

		// Configure ribbon trail parameters for a sword-swing style effect
		RibbonTrailParameters ribbonParams;
		ribbonParams.maxSegments = 48;            // Maximum segments in the trail
		ribbonParams.segmentInterval = 0.016f;    // Add segment every ~16ms (60 fps)
		ribbonParams.segmentLifetime = 0.5f;      // Trail fades over 0.5 seconds
		ribbonParams.initialWidth = 1.5f;         // Wide at the emitter
		ribbonParams.finalWidth = 0.0f;           // Taper to nothing
		ribbonParams.initialColor = Vector4(0.2f, 0.6f, 1.0f, 1.0f);   // Bright cyan-blue
		ribbonParams.finalColor = Vector4(0.1f, 0.2f, 0.8f, 0.0f);     // Fade to transparent blue
		ribbonParams.faceCamera = true;           // Billboard mode
		ribbonParams.textureRepeat = 1.0f;        // One texture repeat along length
		ribbonParams.minSegmentDistance = 0.05f;  // Minimum movement to add segment

		g_ribbonTrail->SetParameters(ribbonParams);

		// Use the particle material for ribbon trail (or create a dedicated one)
		MaterialPtr ribbonMaterial = RibbonTrail::GetDefaultMaterial(true);
		if (ribbonMaterial)
		{
			g_ribbonTrail->SetMaterial(ribbonMaterial);
		}
		else if (particleMaterial)
		{
			// Fallback to particle material
			g_ribbonTrail->SetMaterial(particleMaterial);
		}

		// Start the ribbon trail
		g_ribbonTrail->Play();

		// Create deferred renderer
		g_deferredRenderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), *g_scene, desc.width, desc.height);

		// Optimize shadow settings for better quality
		g_deferredRenderer->SetDepthBias(138.6f, 0.1f, 0.0f); // Hardware depth bias settings
		g_deferredRenderer->SetShadowBias(0.00002f);           // Software depth bias in shadow space
		g_deferredRenderer->SetNormalBiasScale(0.2f);         // Scale factor for normal-based bias
		g_deferredRenderer->SetBlockerSearchRadius(0.03f);     // Blocker search radius for PCSS-style shadows
		g_deferredRenderer->SetLightSize(0.0268f);              // Virtual light size for penumbra simulation
		
		// Set shadow map information and direction in the scene
		g_scene->SetShadowDirLightTextureOffset(0.5f);
		g_scene->SetShadowDirectionalLightExtrusionDistance(50.0f);

		g_terrain = std::make_unique<terrain::Terrain>(*g_scene, g_camera, 64, 64);
		g_terrain->SetBaseFileName("GraphicsTest");
		g_terrain->SetDefaultMaterial(MaterialManager::Get().Load("Models/Default.hmat"));		// Models/FalwynPlains_Terrain_Forest.hmi

		DLOG("Camera dir: " << g_camera->GetDerivedDirection());

		for (uint32 i = 31; i < 33; ++i)
		{
			for (uint32 j = 31; j < 33; ++j)
			{
				if (terrain::Page* page = g_terrain->GetPage(i, j))
				{
					page->Prepare();
					while (!page->Load())
					{
					}
				}
			}
		}

		const uint32 color = 0xFFFFFFFF;
		const POS_COL_TEX_VERTEX vertices[6]{
			{ { -1.0f,	-1.0f,		0.0f }, color, { 0.0f, 1.0f } },
			{ { -1.0f,	1.0f,		0.0f }, color, { 0.0f, 0.0f } },
			{ { 1.0f,	1.0f,		0.0f }, color, { 1.0f, 0.0f } },
			{ { 1.0f,	1.0f,		0.0f }, color, { 1.0f, 0.0f } },
			{ { 1.0f,	-1.0f,		0.0f }, color, { 1.0f, 1.0f } },
			{ { -1.0f,	-1.0f,		0.0f }, color, { 0.0f, 1.0f } }
		};

		g_quadBuffer = GraphicsDevice::Get().CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::StaticWriteOnly, vertices);

		g_foliage = std::make_unique<Foliage>(*g_scene, GraphicsDevice::Get());

		g_foliage->SetTerrainSampleCallback([](float x, float z, FoliagePlacementSample& out)
			{
				out.height = 0.0f;
				out.normal = Vector3::UnitY;
				out.valid = true;
				return true;
			});

		g_foliage->SetBounds(AABB(Vector3(-50.0f, -100.0f, -50.0f), Vector3(50.0f, 200.0f, 50.0f)));

		// Load a grass mesh
		MeshPtr grassMesh = MeshManager::Get().Load("Models/FalwynPlains/Plants/Grass_01.hmsh");

		FoliageSettings foliageSettings;
		foliageSettings.chunkSize = 32.0f;              // Size of each chunk in world units
		foliageSettings.maxViewDistance = 150.0f;       // Maximum view distance
		foliageSettings.loadRadius = 5;                 // Chunks to load around camera
		foliageSettings.frustumCulling = true;          // Enable frustum culling
		foliageSettings.globalDensityMultiplier = 1.0f; // Global density scale
		g_foliage->SetSettings(foliageSettings);

		// Create a layer with default settings
		auto grassLayer = std::make_shared<FoliageLayer>("Grass", grassMesh);

		// Configure layer settings
		FoliageLayerSettings& layerSettings = grassLayer->GetSettings();
		layerSettings.density = 2.0f;           // 2 instances per square unit
		layerSettings.minScale = 0.7f;          // Minimum random scale
		layerSettings.maxScale = 1.3f;          // Maximum random scale
		layerSettings.maxSlopeAngle = 30.0f;    // No grass on steep slopes
		layerSettings.fadeStartDistance = 40.0f;
		layerSettings.fadeEndDistance = 80.0f;
		layerSettings.castShadows = false;      // Disable shadows for performance

		// Add to foliage system
		g_foliage->AddLayer(grassLayer);

		if (g_repro.enabled)
		{
			SetupFogRepro();
		}

		return true;
	}

	void DestroyGlobal()
	{
		g_foliage.reset();
		g_terrain.reset();

		if (g_ribbonTrail)
		{
			g_scene->DestroyRibbonTrail(*g_ribbonTrail);
			g_ribbonTrail = nullptr;
		}
		if (g_ribbonTrailNode)
		{
			g_scene->DestroySceneNode(*g_ribbonTrailNode);
			g_ribbonTrailNode = nullptr;
		}

		if (g_particleEmitter)
		{
			g_scene->DestroyParticleEmitter(*g_particleEmitter);
			g_particleEmitter = nullptr;
		}
		if (g_particleNode)
		{
			g_scene->DestroySceneNode(*g_particleNode);
			g_particleNode = nullptr;
		}

		if (g_lightDebugEnt)
		{
			g_scene->DestroyEntity(*g_lightDebugEnt);
			g_lightDebugEnt = nullptr;
		}
		if (g_pointLight2Node)
		{
			g_scene->DestroySceneNode(*g_pointLight2Node);
			g_pointLight2Node = nullptr;
		}
		if (g_pointLight2)
		{
			g_scene->DestroyLight(*g_pointLight2);
			g_pointLight2 = nullptr;
		}
		if (g_pointLight)
		{
			g_scene->DestroyLight(*g_pointLight);
			g_pointLight = nullptr;
		}
		if (g_pointLightNode)
		{
			g_scene->DestroySceneNode(*g_pointLightNode);
			g_pointLightNode = nullptr;
		}
		if (g_pointLightRotator)
		{
			g_scene->DestroySceneNode(*g_pointLightRotator);
			g_pointLightRotator = nullptr;
		}

		if (g_boarNode)
		{
			g_scene->DestroySceneNode(*g_boarNode);
			g_boarNode = nullptr;
		}
		
		if (g_boarEntity)
		{
			g_scene->DestroyEntity(*g_boarEntity);
			g_boarEntity = nullptr;
		}
		
		if (g_cameraNode)
		{
			g_scene->DestroySceneNode(*g_cameraNode);
			g_cameraNode = nullptr;
		}

		if (g_camera)
		{
			g_scene->DestroyCamera(*g_camera);
			g_camera = nullptr;
		}
		
		g_deferredRenderer.reset();
		g_scene.reset();
	}

	int CommonMain()
	{
#if defined(_WIN32) && defined(_DEBUG)
		std::mutex logMutex;
		mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry& entry) {
			std::scoped_lock lock{ logMutex };
			OutputDebugStringA((entry.message + "\n").c_str());
			});
#endif
        
		// Setup scene and everything
		if (InitializeGlobal())
		{
			// Run event loop
			EventLoop::Run();
		}

		// Destroy everything
		DestroyGlobal();

		// Cut all connected signals
		signals.disconnect();

		// Destroy event loop
		EventLoop::Destroy();

		return 0;
	}
}

#ifdef __APPLE__
int main_osx(int argc, char* argv[]);
#endif

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int argc, char** argv)
#endif
{
#ifdef __APPLE__
    return main_osx(argc, argv);
#else
	return mmo::CommonMain();
#endif
}
