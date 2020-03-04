// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_component.h"
#include "font.h"

#include "base/signal.h"


namespace mmo
{
	/// This class is a texture frame object which can be used to render plain images.
	class TextComponent final
		: public FrameComponent
	{
	public:
		/// Creates a frame font string object which can be used to draw a text.
		explicit TextComponent(Frame& frame, const std::string& fontFile, float fontSize, float outline = 0.0f);

	public:
		// FrameComponent overrides
		void Render(GeometryBuffer& buffer) const override;
		Size GetSize() const override;

	private:
		/// Executed when the frame's text changed.
		void OnTextChanged();

	private:
		/// The graphics texture object.
		FontPtr m_font;
		/// The cached pixel width of the string.
		float m_width;
		/// Scoped frame signal connection
		scoped_connection m_frameConnection;
	};
}
