// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "ui/model_frame.h"

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
		ModelFrame* m_modelFrame;
		scoped_connection m_frameRenderEndCon;
	};
}
