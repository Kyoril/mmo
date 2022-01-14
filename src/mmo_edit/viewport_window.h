// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "graphics/render_texture.h"
#include "graphics/vertex_buffer.h"
#include "graphics/index_buffer.h"
#include "math/vector3.h"
#include "math/quaternion.h"

#include "scene_graph/scene.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/axis_display.h"

#include "imgui.h"
#include "math/matrix4.h"

namespace mmo
{
	/// Manages the game viewport window in the editor.
	class ViewportWindow final
		: public NonCopyable
	{
	public:
		explicit ViewportWindow();
		~ViewportWindow() override;

	public:
		/// Renders the actual 3d viewport content.
		void Render();

		void SetMesh(VertexBufferPtr vertBuf, IndexBufferPtr indexBuf);

		void MoveCamera(const Vector3& offset);
		void MoveCameraTarget(const Vector3& offset);

	public:
		/// Draws the viewport window.
		bool Draw();
		/// Draws the view menu item for this window.
		bool DrawViewMenuItem();

	public:
		/// Makes the viewport window visible.
		void Show() noexcept { m_visible = true; }
		/// Determines whether the viewport window is currently visible.
		bool IsVisible() const noexcept { return m_visible; }
				
	private:
		bool m_visible;
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
		VertexBufferPtr m_vertBuf;
		IndexBufferPtr m_indexBuf;
		bool m_wireFrame;

		Scene m_scene;
		SceneNode* m_cameraAnchor { nullptr };
		SceneNode* m_cameraNode { nullptr };
		Camera* m_camera { nullptr };
		std::unique_ptr<AxisDisplay> m_axisDisplay;
		std::unique_ptr<WorldGrid> m_worldGrid;
	};
}
