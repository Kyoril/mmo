// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "ui/world_frame.h"

#include "frame_ui/frame_renderer.h"
#include "graphics/render_texture.h"
#include "base/signal.h"

#include "deferred_shading/deferred_renderer.h"


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

		DeferredRenderer* GetDeferredRenderer() const { return m_deferredRenderer.get(); }

	private:
		Rect m_lastFrameRect;
		WorldFrame* m_worldFrame;
		scoped_connection m_frameRenderEndCon;

		Scene& m_worldScene;
		Camera* m_camera;

		std::unique_ptr<DeferredRenderer> m_deferredRenderer;

		/// The texture the frame's quad currently shows. The deferred renderer's final target is
		/// not one fixed texture: it is the underwater post-process output while the camera is
		/// submerged, and that target is created on the first dive and released after surfacing.
		/// The quad has to be rebuilt whenever this changes, or the frame keeps showing a stale
		/// texture - which is how the underwater effect rendered every frame and never appeared.
		TexturePtr m_displayedTexture;

		/// Internal (pre-upscale) render resolution the deferred renderer is currently sized to.
		/// Driven by the gxRenderScale console variable; the result is upscaled to the frame size.
		uint16 m_internalWidth = 0;
		uint16 m_internalHeight = 0;
	};
}
