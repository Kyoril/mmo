
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

namespace mmo
{
	std::unique_ptr<Scene> g_scene;
	Camera* g_camera = nullptr;
	SceneNode* g_cameraNode = nullptr;

	SceneNode* g_boarNode = nullptr;
	Entity* g_boarEntity = nullptr;

	RenderTexturePtr g_sceneRenderTarget = nullptr;
	VertexBufferPtr g_fullScreenQuadBuffer = nullptr;

	void RenderScene()
	{
		GraphicsDevice& gx = GraphicsDevice::Get();

		gx.CaptureState();

		// Activate our scene render target
		g_sceneRenderTarget->Activate();
		g_sceneRenderTarget->Clear(ClearFlags::All);

		// Render our scene
		g_scene->Render(*g_camera);
		g_sceneRenderTarget->Update();

		gx.RestoreState();
	}

	void OnIdle(float elapsedTime, GameTime time)
	{
		// Update scene objects and stuff

	}

	void OnPaint()
	{
		RenderScene();

		GraphicsDevice& gx = GraphicsDevice::Get();

		// Render our full screen quad with the scene render target assigned to it
		gx.SetTransformMatrix(World, Matrix4::Identity);
		gx.SetTransformMatrix(View, Matrix4::Identity);
		gx.SetTransformMatrix(Projection, Matrix4::Identity);
		gx.SetVertexFormat(VertexFormat::PosColorTex1);
		gx.SetTopologyType(TopologyType::TriangleList);
		gx.SetTextureFilter(TextureFilter::None);
		gx.SetTextureAddressMode(TextureAddressMode::Clamp);
		gx.BindTexture(g_sceneRenderTarget, ShaderType::PixelShader, 0);

		g_fullScreenQuadBuffer->Set(0);
		gx.Draw(6);
	}

	bool Setup()
	{
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

		// Create the render target for our scene
		g_sceneRenderTarget = GraphicsDevice::Get().CreateRenderTexture("Scene", 1280, 800);

		const POS_COL_TEX_VERTEX vertices[6] = {
			{ Vector3(-1.0f, -1.0f, 0.0f), Color::White, {0.0f, 1.0f}},
			{ Vector3(1.0f, -1.0f, 0.0f), Color::White, {1.0f, 1.0f}},
			{ Vector3(-1.0f, 1.0f, 0.0f), Color::White, {0.0f, 0.0f}},
			{ Vector3(-1.0f, 1.0f, 0.0f), Color::White, {0.0f, 0.0f}},
			{ Vector3(1.0f, -1.0f, 0.0f), Color::White, {1.0f, 1.0f}},
			{ Vector3(1.0f, 1.0f, 0.0f), Color::White, {1.0f, 0.0f}}
		};

		g_fullScreenQuadBuffer = GraphicsDevice::Get().CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::Static, vertices);

		return true;
	}

	void Destroy()
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
		
		g_sceneRenderTarget.reset();
		g_scene.reset();
	}

	int CommonMain()
	{
		// Describe our graphics device
		GraphicsDeviceDesc desc {};
		desc.width = 1280;
		desc.height = 800;
		desc.windowed = true;
		desc.vsync = true;
#ifdef _WIN32
		GraphicsDevice::CreateD3D11(desc);
#endif

		const RenderWindowPtr window = GraphicsDevice::Get().GetAutoCreatedWindow();
		ASSERT(window);

		scoped_connection_container signals;

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

		// Setup scene and everything
		if (Setup())
		{
			// Run event loop
			EventLoop::Run();
		}

		// Destroy everything
		Destroy();

		// Cut all connected signals
		signals.disconnect();

		// Destroy event loop
		EventLoop::Destroy();

		return 0;
	}
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int argc, char** argv[])
#endif
{
	return mmo::CommonMain();
}
