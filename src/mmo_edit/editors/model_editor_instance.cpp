
#include "model_editor_instance.h"
#include "model_editor.h"
#include "editor_host.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	ModelEditorInstance::ModelEditorInstance(ModelEditor& editor, Path asset)
		: EditorInstance(std::move(asset))
		, m_editor(editor)
		, m_wireFrame(false)
	{
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 15.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-45.0f), Vector3::UnitX));

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "DebugAxis");
			m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());

		m_entity = m_scene.CreateEntity("Entity", GetAssetPath().string());
		if (m_entity)
		{
			m_scene.GetRootSceneNode().AttachObject(*m_entity);
		}

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &ModelEditorInstance::Render);
	}

	ModelEditorInstance::~ModelEditorInstance()
	{
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
		}

		m_worldGrid.reset();
		m_axisDisplay.reset();
		m_scene.Clear();
	}

	void ModelEditorInstance::Render()
	{
		if (!m_viewportRT) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		auto& gx = GraphicsDevice::Get();

		// Render the scene first
		gx.Reset();
		gx.SetClearColor(Color::Black);
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);

		gx.SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);
		
		m_scene.Render(*m_camera);
		
		m_viewportRT->Update();
	}

	void ModelEditorInstance::Draw()
	{
		// Determine the current viewport position
		auto viewportPos = ImGui::GetWindowContentRegionMin();
		viewportPos.x += ImGui::GetWindowPos().x;
		viewportPos.y += ImGui::GetWindowPos().y;

		// Determine the available size for the viewport window and either create the render target
		// or resize it if needed
		const auto availableSpace = ImGui::GetContentRegionAvail();
		
		if (m_viewportRT == nullptr)
		{
			m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("Viewport", std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y));
			m_lastAvailViewportSize = availableSpace;
		}
		else if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
		{
			m_viewportRT->Resize(availableSpace.x, availableSpace.y);
			m_lastAvailViewportSize = availableSpace;
		}

		// Render the render target content into the window as image object
		ImGui::Image(m_viewportRT->GetTextureObject(), availableSpace);
	}
}
