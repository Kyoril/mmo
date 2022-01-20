#pragma once

#include "base/signal.h"

#include <imgui.h>

#include "editor_instance.h"
#include "graphics/render_texture.h"
#include "scene_graph/scene.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/world_grid.h"

namespace mmo
{
	class ModelEditor;
	class Entity;
	class Camera;
	class SceneNode;

	class ModelEditorInstance final : public EditorInstance
	{
	public:
		explicit ModelEditorInstance(ModelEditor& editor, Path asset);
		~ModelEditorInstance() override;

	public:
		/// Renders the actual 3d viewport content.
		void Render();

		void Draw() override;

	private:
		ModelEditor& m_editor;
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
		bool m_wireFrame;

		Scene m_scene;
		SceneNode* m_cameraAnchor { nullptr };
		SceneNode* m_cameraNode { nullptr };
		Entity* m_entity { nullptr };
		Camera* m_camera { nullptr };
		std::unique_ptr<AxisDisplay> m_axisDisplay;
		std::unique_ptr<WorldGrid> m_worldGrid;
	};
}
