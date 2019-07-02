// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_object.h"
#include "font.h"

#include "graphics/texture.h"


namespace mmo
{
	/// This class is a texture frame object which can be used to render plain images.
	class FrameFontString : public FrameObject
	{
	public:
		/// Creates a frame font string object which can be used to draw a text.
		explicit FrameFontString(const std::string& fontFile, float fontSize, float outline = 0.0f);

	public:
		// FrameObject overrides
		void Render(GeometryBuffer& buffer) const override;

	public:
		/// Sets the new string to render.
		void SetText(const std::string& text);

	public:
		/// Gets the current text value.
		inline const std::string& GetText() const { return m_text; }

	private:
		/// The graphics texture object.
		FontPtr m_font;
		/// The string value to render.
		std::string m_text;
		/// The cached pixel width of the string.
		float m_width;
	};
}
