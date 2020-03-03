// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_component.h"
#include "font.h"


namespace mmo
{
	/// This class is a texture frame object which can be used to render plain images.
	class TextComponent 
		: public FrameComponent
	{
	public:
		/// Creates a frame font string object which can be used to draw a text.
		explicit TextComponent(Frame& frame, const std::string& fontFile, float fontSize, float outline = 0.0f);

	public:
		// FrameComponent overrides
		void Render(GeometryBuffer& buffer) const override;

	public:
		/// Sets the new string to render.
		void SetText(const std::string& text);

	public:
		/// Gets the current text value.
		inline const std::string& GetText() const { return m_text; }

	public:
		virtual Size GetSize() const override;

	private:
		/// The graphics texture object.
		FontPtr m_font;
		/// The string value to render.
		std::string m_text;
		/// The cached pixel width of the string.
		float m_width;
	};
}
