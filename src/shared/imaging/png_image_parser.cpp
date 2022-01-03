// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "png_image_parser.h"

#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "lodepng/lodepng.h"
#include "log/default_log_levels.h"

namespace mmo
{
	bool PngImageParser::Parse(std::istream & data, SourceImageInfo& out_info, ImageData& out_data)
	{
		// Setup source and  reader
		io::StreamSource source{ data };


		std::vector<uint8> png;
		png.resize(source.size(), 0);
		source.read(reinterpret_cast<char*>(&png[0]), png.size());

		std::vector<uint8> raw;
		unsigned w, h;
		lodepng::decode(raw, w, h, png, LCT_RGBA, 8);
				
		// While parsing the image data, we check every pixel for it's alpha channel and 
		// if none has any alpha value less than 255, the image is fully opaque and thus
		// can be compressed more efficiently eventually.
		bool fullyOpaque = true;

		out_info.width = static_cast<uint16>(w);
		out_info.height = static_cast<uint16>(h);
		out_info.format = ImageFormat::RGBA;
		
		out_data.resize(w * h * 4, 0xff);
		for(auto y = 0ull; y < h; ++y)
		{
			for(auto x = 0ull; x < w; ++x)
			{
				const auto rawIndex = (y * h + x) * 4;
				const auto targetIndex = ((h - y - 1) * h + x) * 4;

				if (!fullyOpaque)
				{
					fullyOpaque = raw[rawIndex + 3] == 255;
				}
				
				out_data[targetIndex]	= raw[rawIndex];
				out_data[targetIndex+1] = raw[rawIndex+1];
				out_data[targetIndex+2] = raw[rawIndex+2];
				out_data[targetIndex+3] = raw[rawIndex+3];
			}
		}

		// Eventually discard alpha channel if every pixel is opaque
		if (fullyOpaque)
		{
			out_info.format = ImageFormat::RGBX;
		}
		
		return true;
	}
}
