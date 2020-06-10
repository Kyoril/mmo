
#pragma once

#include "frame_ui/frame_renderer.h"
#include "graphics/render_texture.h"
#include "graphics/vertex_buffer.h"
#include "graphics/index_buffer.h"
#include "base/signal.h"


namespace mmo
{
	/// This class renders a model into a texture which is then rendered in the frame as content.
	class ModelRenderer final
		: public FrameRenderer
	{
	public:
		ModelRenderer(const std::string& name);

	public:
		/// Renders a given frame using this renderer instance.
		/// @param frame The frame instance to render.
		/// @param colorOverride An optional color override for tinting.
		/// @param clipper An optional clip rect.
		virtual void Render(
			optional<Color> colorOverride = optional<Color>(),
			optional<Rect> clipper = optional<Rect>()) final override;
		/// Called to notify the renderer that a frame has been attached.
		virtual void NotifyFrameAttached() final override;
		/// Called to notify the renderer that a frame has been detached.
		virtual void NotifyFrameDetached() final override;

	private:
		RenderTexturePtr m_renderTexture;

		Rect m_lastFrameRect;
		scoped_connection m_frameRenderEndCon;

		VertexBufferPtr m_vBuffer;
		IndexBufferPtr m_iBuffer;
	};
}
