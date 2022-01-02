// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_component.h"
#include "font.h"

#include "base/typedefs.h"


namespace mmo
{
	/// Enumerated type used when specifying vertical alignments.
	enum class VerticalAlignment : uint8
	{
		/// Frame's position specifies an offset of it's top edge from the top edge of it's parent.
		Top,
		/// Frame's position specifies an offset of it's vertical center from the vertical center of it's parent.
		Center,
		/// Frame's position specifies an offset of it's bottom edge from the bottom edge of it's parent.
		Bottom
	};

	/// Parses a string and converts it to a VerticalAlignment enum value.
	VerticalAlignment VerticalAlignmentByName(const std::string& name);
	/// Generates the name of a VerticalAlignment enum value.
	std::string VerticalAlignmentName(VerticalAlignment alignment);

	/// Enumerated type used when specifying horizontal alignments.
	enum class HorizontalAlignment : uint8
	{
		/// Frame's position specifies an offset of it's left edge from the left edge of it's parent.
		Left,
		/// Frame's position specifies an offset of it's horizontal center from the horizontal center of it's parent.
		Center,
		/// Frame's position specifies an offset of it's right edge from the right edge of it's parent.
		Right
	};

	/// Parses a string and converts it to a HorizontalAlignment enum value.
	HorizontalAlignment HorizontalAlignmentByName(const std::string& name);
	/// Generates the name of a HorizontalAlignment enum value.
	std::string HorizontalAlignmentName(HorizontalAlignment alignment);

	/// This class is a texture frame object which can be used to render plain images.
	class TextComponent final
		: public FrameComponent
	{
	public:
		/// Creates a frame font string object which can be used to draw a text.
		explicit TextComponent(Frame& frame);

	public:
		virtual std::unique_ptr<FrameComponent> Copy() const override;

	public:
		/// Gets the horizontal text alignment.
		inline HorizontalAlignment GetHorizontalAlignment() const { return m_horzAlignment; }
		/// Sets the horizontal text alignment.
		void SetHorizontalAlignment(HorizontalAlignment alignment);
		/// Gets the vertical text alignment.
		inline VerticalAlignment GetVerticalAlignment() const { return m_vertAlignment; }
		/// Sets the vertical text alignment.
		void SetVerticalAlignment(VerticalAlignment alignment);
		/// Gets the global text color.
		inline const Color& GetColor() const { return m_color; }
		/// Sets the global text color.
		void SetColor(const Color& color);
		/// Determines the number of lines.
		inline uint32 GetLineCount() const noexcept { return m_lineCache.size(); }

	private:
		/// This function caches some values required for rendering the text using this component's properites.
		void CacheText(const Rect& area);
		/// Applies text wrapping to the line cache.
		void ApplyWrapping(const Rect& frameRect);

	public:
		// FrameComponent overrides
		void Render(const Rect& area, const Color& color = Color::White) override;

	private:
		/// The color to use when rendering text.
		Color m_color = Color(0xffffffff);
		/// 
		HorizontalAlignment m_horzAlignment = HorizontalAlignment::Left;
		/// 
		VerticalAlignment m_vertAlignment = VerticalAlignment::Top;
		/// A cache for text lines with wrapping applied.
		std::vector<std::string> m_lineCache;
	};
}
