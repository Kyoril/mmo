// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <istream>
#include <vector>


namespace mmo
{
	/// Enumerates available pixel formats for source files.
	enum class ImageFormat
	{
		/// Parser is expected to return 32 bit pixel data, but the alpha channel isn't 
		/// filled with meaningful data and thus discarded. This might be important when
		/// choosing the final pixel format to write.
		RGBX,
		/// 32 bit rgba pixel data with 8 bits per channel. Uncompressed.
		RGBA,
		/// DXT1/BC1 compressed data.
		DXT1,
		/// DXT5/BC3 compressed data.
		DXT5
	};


	/// Contains infos about a source image file.
	struct SourceImageInfo final
	{
		/// Actual width of the image in texels.
		uint16 width;
		/// Actual height of the image in texels.
		uint16 height;
		/// Image texel data format.
		ImageFormat format;
	};


	/// Structure that contains image data.
	typedef std::vector<uint8> ImageData;


	/// This interface is the base class for parsing an image file.
	struct IImageParser
	{
		virtual ~IImageParser() = default;

		/// Parses image data.
		/// @param data The input data stream to read from.
		/// @param out_info Structure that will be filled with infos about the source image.
		/// @param out_data Output vector that is filled with texel data of the source image, eventually converted.
		/// @return true on success, false if an error occurred.
		virtual bool Parse(std::istream& data, SourceImageInfo& out_info, ImageData& out_data) = 0;
	};
}
