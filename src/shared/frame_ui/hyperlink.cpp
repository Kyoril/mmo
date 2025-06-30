// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "hyperlink.h"
#include "inline_color.h"
#include "utf8_utils.h"

#include <sstream>

namespace mmo
{
	ParsedText ParseTextMarkup(const std::string& text, argb_t defaultColor)
	{
		ParsedText result;
		result.plainText.reserve(text.length()); // Reserve space to avoid frequent reallocations
		
		argb_t currentColor = defaultColor;
		std::size_t idx = 0;
		std::size_t plainTextPos = 0;
		
		while (idx < text.length())
		{
			// Check for markup starting with |
			if (idx + 1 < text.length() && text[idx] == '|')
			{
				const char directive = text[idx + 1];
				
				// Handle color codes |cAARRGGBB
				if ((directive == 'c' || directive == 'C') && idx + 9 < text.length())
				{
					// Parse color code
					std::string hexColor = text.substr(idx + 2, 8);
					argb_t newColor = static_cast<argb_t>(std::strtoul(hexColor.c_str(), nullptr, 16));
					
					// Only add color change if it's different from current
					if (newColor != currentColor)
					{
						currentColor = newColor;
						result.colorChanges.emplace_back(plainTextPos, currentColor);
					}
					
					idx += 10; // Skip |c + 8 hex digits
					continue;
				}
				// Handle color reset |r
				else if (directive == 'r' || directive == 'R')
				{
					// Reset to default color
					if (currentColor != defaultColor)
					{
						currentColor = defaultColor;
						result.colorChanges.emplace_back(plainTextPos, currentColor);
					}
					
					idx += 2; // Skip |r
					continue;
				}
				// Handle hyperlink start |Htype:payload|h
				else if ((directive == 'h' || directive == 'H'))
				{
					// Look for the hyperlink format: |Htype:payload|h[displaytext]|h
					std::size_t hyperlinkStart = idx;
					idx += 2; // Skip |H
					
					// Find the end of the type:payload part (next |h)
					std::size_t payloadEnd = text.find("|h", idx);
					if (payloadEnd == std::string::npos)
					{
						// Invalid hyperlink format, treat as normal text
						result.plainText += text[hyperlinkStart];
						plainTextPos++;
						idx = hyperlinkStart + 1;
						continue;
					}
					
					// Extract type:payload
					std::string typePayload = text.substr(idx, payloadEnd - idx);
					
					// Find the colon separator
					std::size_t colonPos = typePayload.find(':');
					if (colonPos == std::string::npos)
					{
						// Invalid format, treat as normal text
						result.plainText += text[hyperlinkStart];
						plainTextPos++;
						idx = hyperlinkStart + 1;
						continue;
					}
					
					std::string linkType = typePayload.substr(0, colonPos);
					std::string linkPayload = typePayload.substr(colonPos + 1);
					
					// Move past the |h
					idx = payloadEnd + 2;
					
					// Find the display text [text] - include the brackets in the display
					if (idx >= text.length() || text[idx] != '[')
					{
						// Invalid format, treat as normal text
						result.plainText += text[hyperlinkStart];
						plainTextPos++;
						idx = hyperlinkStart + 1;
						continue;
					}
					
					std::size_t displayTextStart = idx; // Include the opening bracket
					
					// Find the closing ]
					std::size_t displayTextEnd = text.find(']', idx + 1);
					if (displayTextEnd == std::string::npos)
					{
						// Invalid format, treat as normal text
						result.plainText += text[hyperlinkStart];
						plainTextPos++;
						idx = hyperlinkStart + 1;
						continue;
					}
					
					// Include the closing bracket in the display text
					std::string displayText = text.substr(displayTextStart, displayTextEnd - displayTextStart + 1);
					idx = displayTextEnd + 1; // Move past ]
					
					// Expect |h to close the hyperlink
					if (idx + 1 >= text.length() || text.substr(idx, 2) != "|h")
					{
						// Invalid format, treat as normal text
						result.plainText += text[hyperlinkStart];
						plainTextPos++;
						idx = hyperlinkStart + 1;
						continue;
					}
					
					idx += 2; // Skip |h
							// Create the hyperlink
					Hyperlink hyperlink(std::move(linkType), std::move(linkPayload), displayText, currentColor);
					hyperlink.startIndex = hyperlinkStart;
					hyperlink.endIndex = idx;

					// Add the display text to the plain text and record position
					std::size_t hyperlinkTextStart = plainTextPos;
					result.plainText += displayText;
					plainTextPos += displayText.length();

					// Store the position in plain text for bounds calculation
					hyperlink.plainTextStart = hyperlinkTextStart;
					hyperlink.plainTextEnd = plainTextPos;

					// Store the hyperlink
					result.hyperlinks.push_back(std::move(hyperlink));
					continue;
				}
			}
			
			// Regular character, add to plain text
			result.plainText += text[idx];
			plainTextPos++;
			idx++;
		}
		
		return result;
	}
}
