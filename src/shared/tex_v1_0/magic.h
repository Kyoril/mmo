// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

				/// DXT1 / BC1 compression (doesn't support alpha, high compression, lossy)
			    DXT1,

				/// DXT5 / BC3 compression (supports alpha, medium compression, lossy)
				DXT5,
				
				FLOAT_RGB,
				FLOAT_RGBA,

				/// Single-channel 8-bit uncompressed (grayscale, alpha masks, heightmaps)
				R8,

				/// Two-channel 8-bit uncompressed (normal maps stored as XY)
				RG8,

				/// BC4 compressed single-channel (compressed grayscale)
				BC4,

				/// BC5 compressed two-channel (compressed normal maps, stores XY)
				BC5,
			};

			/// Returns a human readable string based on the pixel format.
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
					return "DXT1 / BC1";
				case PixelFormat::DXT5:
					return "DXT5 / BC3";
				case PixelFormat::FLOAT_RGB:
					return "Float RGB";
				case PixelFormat::FLOAT_RGBA:
					return "Float RGBA";
				case PixelFormat::R8:
					return "R8 (Single Channel)";
				case PixelFormat::RG8:
					return "RG8 (Two Channel)";
				case PixelFormat::BC4:
					return "BC4 (Compressed Single Channel)";
				case PixelFormat::BC5:
					return "BC5 (Compressed Normal Map)";
				default:
					return "(unknown)";
				}
			}
		}
	}
}
