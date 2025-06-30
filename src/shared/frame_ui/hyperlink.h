// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <vector>
#include <memory>

#include "color.h"
#include "rect.h"

namespace mmo
{
	/// Represents a hyperlink in text with type, payload, and display text
	struct Hyperlink
	{
		/// The type of the hyperlink (e.g., "item", "spell", "quest", etc.)
		std::string type;
		
		/// The payload data for the hyperlink (e.g., item ID, spell ID, etc.)
		std::string payload;
		
		/// The display text shown to the user
		std::string displayText;
		
		/// The color of the hyperlink text
		argb_t color;
		
		/// The bounding rectangle of the hyperlink (for click detection)
		Rect bounds;
		
		/// The start index in the original text (including markup)
		std::size_t startIndex;
		
		/// The end index in the original text (including markup)
		std::size_t endIndex;
		
		/// The start position in the plain text (for bounds calculation)
		std::size_t plainTextStart;
		
		/// The end position in the plain text (for bounds calculation)
		std::size_t plainTextEnd;

		/// Constructor
		Hyperlink(std::string type, std::string payload, std::string displayText, argb_t color)
			: type(std::move(type))
			, payload(std::move(payload))
			, displayText(std::move(displayText))
			, color(color)
			, bounds()
			, startIndex(0)
			, endIndex(0)
			, plainTextStart(0)
			, plainTextEnd(0)
		{
		}
	};

	/// Contains the result of parsing text with inline formatting
	struct ParsedText
	{
		/// The plain text without markup
		std::string plainText;
		
		/// List of hyperlinks found in the text
		std::vector<Hyperlink> hyperlinks;
		
		/// Color changes mapped to character positions in plain text
		std::vector<std::pair<std::size_t, argb_t>> colorChanges;
	};

	/// Parses text containing inline color codes and hyperlinks
	/// Format: |caarrggbb|Htype:payload|h[displaytext]|h|r
	/// Where:
	/// - |caarrggbb sets the color (same as before)
	/// - |Htype:payload|h starts a hyperlink with type and payload
	/// - [displaytext] is the text shown to the user
	/// - |h ends the hyperlink
	/// - |r resets the color (same as before)
	ParsedText ParseTextMarkup(const std::string& text, argb_t defaultColor);

	/// Legacy function for backward compatibility
	/// Returns true if a color tag was consumed
	bool ConsumeColourTag(const std::string& txt, std::size_t& idx, argb_t& currentColor, argb_t defaultColor);
}
