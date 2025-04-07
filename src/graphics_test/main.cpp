
#ifdef _WIN32
#	include<Windows.h>
#else
#endif

#include "graphics/graphics_device.h"
#include "event_loop.h"
#include "assets/asset_registry.h"
#include "scene_graph/scene.h"
#include "graphics/texture.h"
#include "graphics/render_target.h"
#include "graphics/render_texture.h"
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

	VertexBufferPtr g_fullScreenQuadBuffer = nullptr;
	std::unique_ptr<DeferredRenderer> g_deferredRenderer = nullptr;

	void RenderScene()
	{
		// Use deferred renderer to render the scene
		g_deferredRenderer->Render(*g_scene, *g_camera);
	}

	void OnIdle(float elapsedTime, GameTime time)
	{
		// Update scene objects and stuff

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
        desc.width = 1280;
        desc.height = 800;
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
		g_camera->SetAspectRatio(1280.0f / 800.0f);

		g_boarNode = g_scene->GetRootSceneNode().CreateChildSceneNode("BoarNode");
		ASSERT(g_boarNode);
		g_boarEntity = g_scene->CreateEntity("Boar", "Models/Creatures/Boar/Boar.hmsh");
		ASSERT(g_boarEntity);
		g_boarNode->AttachObject(*g_boarEntity);

		// Create a directional light for the scene
		Light& directionalLight = g_scene->CreateLight("MainLight", LightType::Directional);
		directionalLight.SetColor(Vector4(1.0f, 0.95f, 0.8f, 1.0f)); // Warm sunlight color
		
		// Create a light node and attach the light
		SceneNode& lightNode = g_scene->CreateSceneNode("MainLightNode");
		lightNode.AttachObject(directionalLight);
		
		// Set the light direction (pointing down and slightly to the side)
		Vector3 direction(-0.5f, -1.0f, -0.3f);
		direction.Normalize();
		lightNode.SetDirection(direction);

		// Create deferred renderer
		g_deferredRenderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), 1280, 800);

		// Create full screen quad for final rendering
		const POS_COL_TEX_VERTEX vertices[6] = {
			{ Vector3(-1.0f, -1.0f, 0.0f), 0xFFFFFFFF, {0.0f, 1.0f}},
			{ Vector3(1.0f, -1.0f, 0.0f), 0xFFFFFFFF, {1.0f, 1.0f}},
			{ Vector3(-1.0f, 1.0f, 0.0f), 0xFFFFFFFF, {0.0f, 0.0f}},
			{ Vector3(-1.0f, 1.0f, 0.0f), 0xFFFFFFFF, {0.0f, 0.0f}},
			{ Vector3(1.0f, -1.0f, 0.0f), 0xFFFFFFFF, {1.0f, 1.0f}},
			{ Vector3(1.0f, 1.0f, 0.0f), 0xFFFFFFFF, {1.0f, 0.0f}}
		};

		g_fullScreenQuadBuffer = GraphicsDevice::Get().CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::Static, vertices);

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
