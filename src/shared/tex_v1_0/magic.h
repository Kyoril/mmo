// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/macros.h"

#include <string>

namespace mmo
{
	namespace tex
	{
		namespace v1_0
		{
			/// Enumerates possible texture compression modes.
			enum PixelFormat
			{
				/// Unknown source image format
				Unknown = 0xff,

				/// Plain uncompressed RGB data.
			    RGB = 0,

				/// Plain uncompressed RGBA data.
				RGBA,

				/// DXT1 compression (doesn't support alpha, high compression, lossy)
			    DXT1,		// Not yet implemented

				/// DXT5 compression (supports alpha, medium compression, lossy)
				DXT5,		// Not yet implemented
				
				FLOAT_RGB,
				FLOAT_RGBA,
			};

			/// Returns a human readable string based on the pixel format.
			inline std::string FormatDescription(PixelFormat format)
			{
				switch(format)
				{
				case PixelFormat::RGB:
					return "RGB";
				case PixelFormat::RGBA:
					return "RGBA";
				case PixelFormat::DXT1:
					return "DXT1";
				case PixelFormat::DXT5:
					return "DXT5";
				default:
					UNREACHABLE();
					return "(unknown)";
				}
			}
		}
	}
}
