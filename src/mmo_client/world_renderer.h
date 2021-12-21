
#pragma once

#include "world_frame.h"
#include "world_grid.h"

#include "frame_ui/frame_renderer.h"
#include "graphics/render_texture.h"
#include "base/signal.h"


namespace mmo
{
	class Scene;
	class Camera;

	/// This class renders a 3d world into a texture which is then rendered in the frame as content.
	class WorldRenderer final
		: public FrameRenderer
	{
	public:
		WorldRenderer(const std::string& name, Scene& worldScene);

	public:
		/// @brief Renders a given frame using this renderer instance.
		/// @param colorOverride An optional color override for tinting.
		/// @param clipper An optional clip rect.
		void Render(
			optional<Color> colorOverride = optional<Color>(),
			optional<Rect> clipper = optional<Rect>()) override;
		/// Called to notify the renderer that a frame has been attached.
		void NotifyFrameAttached() override;
		/// Called to notify the renderer that a frame has been detached.
		void NotifyFrameDetached() override;

	private:
		RenderTexturePtr m_renderTexture;
		Rect m_lastFrameRect;
		WorldFrame* m_worldFrame;
		scoped_connection m_frameRenderEndCon;

		Scene& m_worldScene;
		Camera* m_camera;

		std::unique_ptr<WorldGrid> m_worldGrid;
	};
}
