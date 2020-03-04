// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_component.h"
#include "font.h"


namespace mmo
{
	/// This class is a texture frame object which can be used to render plain images.
	class TextComponent final
		: public FrameComponent
	{
	public:
		/// Creates a frame font string object which can be used to draw a text.
		explicit TextComponent(const std::string& fontFile, float fontSize, float outline = 0.0f);

	public:
		// FrameComponent overrides
		void Render(Frame& frame) const override;

	private:
		/// The graphics texture object.
		FontPtr m_font;
	};
}
