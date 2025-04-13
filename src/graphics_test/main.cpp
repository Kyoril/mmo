
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
#include "scene_graph/material_manager.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

namespace mmo
{
	std::unique_ptr<Scene> g_scene;
	Camera* g_camera = nullptr;
	SceneNode* g_cameraNode = nullptr;

	SceneNode* g_boarNode = nullptr;
	Entity* g_boarEntity = nullptr;

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

	void RenderScene()
	{
		GraphicsDevice::Get().Reset();

		// Use deferred renderer to render the scene
		g_deferredRenderer->Render(*g_scene, *g_camera);

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
			}
			if (key == 'G')
			{
				// Toggle directional light
				g_pointLight2->SetVisible(!g_pointLight2->IsVisible());
			}

        	return true;
        });

		// Setup scene
		g_scene = std::make_unique<OctreeScene>();

		// Create a camera
		g_camera = g_scene->CreateCamera("MainCamera");
		g_cameraNode = &g_scene->CreateSceneNode("MainCameraNode");
		g_cameraNode->AttachObject(*g_camera);
		g_cameraNode->SetPosition(Vector3(0.0f, 8.5f, 15.0f));
		g_cameraNode->LookAt(Vector3::Zero, TransformSpace::Parent);
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
		g_sunLightNode = g_scene->GetRootSceneNode().CreateChildSceneNode("SunLightNode");
		g_sunLightNode->AttachObject(*g_sunLight);
		g_sunLightNode->SetDirection({ -0.5f, -1.0f, -0.3f });

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

		// Create deferred renderer
		g_deferredRenderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), desc.width, desc.height);

		g_terrain = std::make_unique<terrain::Terrain>(*g_scene, g_camera, 64, 64);
		g_terrain->SetBaseFileName("GraphicsTest");
		g_terrain->SetDefaultMaterial(MaterialManager::Get().Load("Models/Default.hmat"));		// Models/FalwynPlains_Terrain_Forest.hmi

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

		return true;
	}

	void DestroyGlobal()
	{
		g_terrain.reset();

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
		std::mutex logMutex;
		mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry& entry) {
			std::scoped_lock lock{ logMutex };
			OutputDebugStringA((entry.message + "\n").c_str());
			});

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
