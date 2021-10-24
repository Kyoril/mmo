// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "tga_image_parser.h"

#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "log/default_log_levels.h"

namespace mmo
{
#ifndef LOWORD
#	define LOWORD(l)           ((uint16)(((uint32)(l)) & 0xffff))
#endif
#ifndef HIWORD
#	define HIWORD(l)           ((uint16)((((uint32)(l)) >> 16) & 0xffff))
#endif

	bool TgaImageParser::Parse(std::istream & data, SourceImageInfo& out_info, ImageData& out_data)
	{
		// Setup source and  reader
		io::StreamSource source{ data };
		io::Reader reader{ source };

		// Read targa header
		TargaHeader header;
		reader.readPOD(header);
		if (!reader)
		{
			ELOG("Failed to read TGA header - source file might be damaged or incomplete!");
			return false;
		}

		// We only support uncompressed tga
		if (header.DataTypeCode != 2)
		{
			ELOG("TGA file is compressed which is currently not supported!");
			return false;
		}

		// We don't support color palettes
		if (header.ColorMapType != 0)
		{
			ELOG("TGA file uses color palettes which are currently not supported");
			return false;
		}

		// We only support 24 or 32 bits
		if (header.BitDepth != 24 && header.BitDepth != 32)
		{
			ELOG("TGA has a bit depth of " << static_cast<uint16>(header.BitDepth) << ", but only 24 and 32 bits are currently supported!");
			return false;
		}

		// Skip image id data
		if (header.IDLength > 0)
		{
			reader.skip(header.IDLength);
			if (!reader)
			{
				ELOG("Failed to read additional TGA header data - the file might be damaged!");
				return false;
			}
		}

		// Obtain image size
		out_info.width = header.wWidth;
		out_info.height = header.wHeight;

		// Detect image format
		out_info.format = header.BitDepth == 24 ? ImageFormat::RGBX : ImageFormat::RGBA;

		// We always return 4 byte per channel
		const size_t byteCount = 4;

		// While parsing the image data, we check every pixel for it's alpha channel and 
		// if none has any alpha value less than 255, the image is fully opaque and thus
		// can be compressed more efficiently eventually.
		bool fullyOpaque = true;

		// Image data: BGR(A)
		out_data.resize(out_info.width * out_info.height * byteCount, 0xff);
		for (uint32 y = 0; y < out_info.height; ++y)
		{
			for (uint32 x = 0; x < out_info.width; ++x)
			{
				uint8 b = 0, g = 0, r = 0, a = 0xff;
				reader >> io::read<uint8>(b) >> io::read<uint8>(g) >> io::read<uint8>(r);
				if (header.BitDepth == 32)
				{
					reader >> io::read<uint8>(a);
					if (a != 0xff)
					{
						fullyOpaque = false;
					}
				}

				if (!reader)
					return false;

				const size_t index = x + y * out_info.width;
				out_data[index * byteCount + 0] = r;
				out_data[index * byteCount + 1] = g;
				out_data[index * byteCount + 2] = b;
				out_data[index * byteCount + 3] = a;
			}
		}

		// Eventually discard alpha channel if every pixel is opaque
		if (fullyOpaque)
		{
			out_info.format = ImageFormat::RGBX;
		}

		// Successfully read tga image
		return true;
	}
}
