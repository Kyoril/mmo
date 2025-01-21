
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

namespace mmo
{
	std::unique_ptr<Scene> g_scene;
	Camera* g_camera = nullptr;
	SceneNode* g_cameraNode = nullptr;
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
		gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::Projection, Matrix4::Identity);
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
		g_scene = std::make_unique<Scene>();

		// Create a camera
		g_camera = g_scene->CreateCamera("MainCamera");
		g_cameraNode = &g_scene->CreateSceneNode("MainCameraNode");
		g_cameraNode->AttachObject(*g_camera);
		g_cameraNode->SetPosition(Vector3(0.0f, 2.0f, -2.0f));
		g_cameraNode->LookAt(Vector3::Zero, TransformSpace::World);

		// Create the render target for our scene
		g_sceneRenderTarget = GraphicsDevice::Get().CreateRenderTexture("Scene", 1280, 800);

		const POS_COL_TEX_VERTEX vertices[6] = {
			{ Vector3(-1.0f, -1.0f, 0.0f), Color::White, {0.0f, 1.0f}},
			{ Vector3(-1.0f, 1.0f, 0.0f), Color::White, {0.0f, 0.0f}},
			{ Vector3(1.0f, 1.0f, 0.0f), Color::White, {1.0f, 0.0f}},
			{ Vector3(-1.0f, -1.0f, 0.0f), Color::White, {0.0f, 1.0f}},
			{ Vector3(1.0f, 1.0f, 0.0f), Color::White, {1.0f, 0.0f}},
			{ Vector3(1.0f, -1.0f, 0.0f), Color::White, {1.0f, 1.0f}}
		};

		g_fullScreenQuadBuffer = GraphicsDevice::Get().CreateVertexBuffer(6, sizeof(POS_COL_TEX_VERTEX), BufferUsage::Static, vertices);

		return true;
	}

	void Destroy()
	{
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
