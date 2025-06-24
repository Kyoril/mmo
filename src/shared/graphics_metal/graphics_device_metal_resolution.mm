// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "graphics_device_metal.h"

#ifdef __APPLE__
#include <AppKit/AppKit.h>
#endif

namespace mmo
{
	std::string GraphicsDeviceMetal::GetPrimaryMonitorResolution() const
	{
#ifdef __APPLE__
		// Get the main screen resolution
		NSScreen* mainScreen = [NSScreen mainScreen];
		if (mainScreen)
		{
			NSRect screenRect = [mainScreen frame];
			int width = static_cast<int>(screenRect.size.width);
			int height = static_cast<int>(screenRect.size.height);
			return std::to_string(width) + "x" + std::to_string(height);
		}
#endif
		// Fallback resolution
		return "1920x1080";
	}

	bool GraphicsDeviceMetal::ValidateFullscreenResolution(uint16 width, uint16 height) const
	{
#ifdef __APPLE__
		// Get the main screen resolution
		NSScreen* mainScreen = [NSScreen mainScreen];
		if (mainScreen)
		{
			NSRect screenRect = [mainScreen frame];
			int maxWidth = static_cast<int>(screenRect.size.width);
			int maxHeight = static_cast<int>(screenRect.size.height);
			
			// Check if requested resolution is within monitor bounds
			if (width > maxWidth || height > maxHeight)
			{
				// Log warning (assuming WLOG macro exists)
				// WLOG("Requested fullscreen resolution " << width << "x" << height << 
				//      " exceeds monitor resolution " << maxWidth << "x" << maxHeight << 
				//      ". Using monitor resolution instead.");
				return false;
			}
			
			// For fullscreen, we should use exact monitor resolution for best performance
			if (width != maxWidth || height != maxHeight)
			{
				// WLOG("Fullscreen resolution " << width << "x" << height << 
				//      " differs from monitor resolution " << maxWidth << "x" << maxHeight << 
				//      ". Consider using monitor resolution for optimal performance.");
			}
		}
#endif
		return true;
	}
}
