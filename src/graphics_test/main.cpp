
#ifdef _WIN32
#	include<Windows.h>
#else
#endif

#include "graphics/graphics_device.h"
#include "event_loop.h"
#include "assets/asset_registry.h"
#include "scene_graph/scene.h"
#include "scene_graph/octree_scene.h"
#include "deferred_renderer.h"
#include "log/default_log.h"
#include "log/log_entry.h"

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

	std::unique_ptr<DeferredRenderer> g_deferredRenderer = nullptr;

	void RenderScene()
	{
		// Use deferred renderer to render the scene
		g_deferredRenderer->Render(*g_scene, *g_camera);
	}

	void OnIdle(float elapsedTime, GameTime time)
	{
		// Update scene objects and stuff
		if (g_pointLightRotator)
		{
			g_pointLightRotator->Yaw(Radian(elapsedTime), TransformSpace::Local);
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
        signals += EventLoop::KeyDown.connect([](const int32 key, bool repeat) { if (key == 0x1B) { EventLoop::Terminate(0); } return true; });

		// Setup scene
		g_scene = std::make_unique<OctreeScene>();

		// Create a camera
		g_camera = g_scene->CreateCamera("MainCamera");
		g_cameraNode = &g_scene->CreateSceneNode("MainCameraNode");
		g_cameraNode->AttachObject(*g_camera);
		g_cameraNode->SetPosition(Vector3(0.0f, 1.5f, 5.0f));
		g_cameraNode->LookAt(Vector3::Zero, TransformSpace::Parent);
		g_camera->SetAspectRatio(1920.0f / 1080.0f);

		g_boarNode = g_scene->GetRootSceneNode().CreateChildSceneNode("BoarNode");
		ASSERT(g_boarNode);
		g_boarEntity = g_scene->CreateEntity("Boar", "Models/Creatures/Boar/Boar.hmsh");
		ASSERT(g_boarEntity);
		g_boarNode->AttachObject(*g_boarEntity);

		// Create a directional light for the scene
		g_sunLight = &g_scene->CreateLight("MainLight", LightType::Directional);
		g_sunLight->SetColor(Vector4(1.0f, 0.95f, 0.8f, 1.0f)); // Warm sunlight color
		g_sunLight->SetIntensity(1.0f);
		g_sunLightNode = &g_scene->CreateSceneNode("SunLightNode");
		g_sunLightNode->AttachObject(*g_sunLight);
		g_sunLightNode->SetDirection({ -0.5f, -1.0f, -0.3f });

		g_pointLightRotator = g_scene->GetRootSceneNode().CreateChildSceneNode("PointLightRotator");

		g_pointLight = &g_scene->CreateLight("PointLight", LightType::Point);
		g_pointLight->SetColor(Vector4(1.0f, 0.0f, 0.0f, 1.0f)); // Red point light
		g_pointLight->SetIntensity(15.0f);
		g_pointLight->SetRange(15.0f);
		g_pointLightNode = g_pointLightRotator->CreateChildSceneNode("PointLightNode", Vector3::UnitZ * 1.0f);
		g_pointLightNode->AttachObject(*g_pointLight);

		// Create deferred renderer
		g_deferredRenderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), desc.width, desc.height);

		return true;
	}

	void DestroyGlobal()
	{
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
