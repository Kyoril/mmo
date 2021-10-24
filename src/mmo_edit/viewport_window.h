// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "graphics/render_texture.h"
#include "graphics/vertex_buffer.h"
#include "graphics/index_buffer.h"
#include "math/vector3.h"
#include "math/quaternion.h"

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

	public:
		/// Renders the actual 3d viewport content.
		void Render() const;

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
		void UpdateProjectionMatrix();
		
	private:
		bool m_visible;
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
		VertexBufferPtr m_vertBuf;
		IndexBufferPtr m_indexBuf;
		Vector3 m_cameraPos;
		Quaternion m_cameraRotation;
		bool m_wireFrame;
		Matrix4 m_projMatrix;
	};
}
