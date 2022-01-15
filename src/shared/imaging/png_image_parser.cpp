// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "png_image_parser.h"

#include <array>

#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "log/default_log_levels.h"

#include "libpng/png.h"

namespace mmo
{
	void ReadPngFromReader(png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead)
	{
		const png_voidp io_ptr = png_get_io_ptr(png_ptr);
		if(io_ptr == nullptr)
			return;   // add custom error handling here

		const io::Reader& reader = *static_cast<io::Reader*>(io_ptr);
		reader.getSource()->read(reinterpret_cast<char*>(outBytes), byteCountToRead);
	}

	bool PngImageParser::Parse(std::istream & data, SourceImageInfo& out_info, ImageData& out_data)
	{
		// Setup source and  reader
		io::StreamSource source{ data };
		io::Reader reader { source };
		
		std::array<uint8, 8> header{};
		reader >> io::read_range(header);

		if (png_sig_cmp(header.data(), 0, 8))
		{
			ELOG("Source file is not a valid PNG file!");
			return false;
		}

		png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
		if (!png_ptr)
		{
			ELOG("Failed to read source PNG file!");
			return false;
		}

		png_infop info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr)
        {
        	png_destroy_read_struct(&png_ptr, nullptr, nullptr);
			ELOG("Failed to read source PNG file!");
			return false;
        }

		if (setjmp(png_jmpbuf(png_ptr)))
		{
			ELOG("Failed to read source PNG file!");
			return false;
		}

		png_set_read_fn(png_ptr, &reader, ReadPngFromReader);
        png_set_sig_bytes(png_ptr, 8);
		png_read_info(png_ptr, info_ptr);

		const auto width = png_get_image_width(png_ptr, info_ptr);
        const auto height = png_get_image_height(png_ptr, info_ptr);
        const auto colorType = png_get_color_type(png_ptr, info_ptr);
        const auto bitDepth = png_get_bit_depth(png_ptr, info_ptr);

		out_info.width = width;
		out_info.height = height;

		const auto number_of_passes = png_set_interlace_handling(png_ptr);
        png_read_update_info(png_ptr, info_ptr);

		if (setjmp(png_jmpbuf(png_ptr)))
		{
			ELOG("Error during read_image");
			return false;
		}

		std::vector<std::vector<png_byte>> rows;
		rows.resize(height);

		const auto rowBytes = png_get_rowbytes(png_ptr, info_ptr);
		for(auto& row : rows)
		{
			row.resize(rowBytes, 0);
		}

		std::vector<png_byte *> prows(height);
		std::transform(rows.begin(), rows.end(), prows.begin(), [](auto & vec){ return vec.data(); });
		
        png_read_image(png_ptr, prows.data());

		if (colorType != PNG_COLOR_TYPE_RGB && colorType != PNG_COLOR_TYPE_RGBA)
		{
			ELOG("Unsupported color type, only RGB and RGBA are supported!");
			return false;
		}
		
		out_info.format = colorType == PNG_COLOR_TYPE_RGB ? ImageFormat::RGBX : ImageFormat::RGBA;
		out_data.resize(width * height * 4, 0xff);

		const size_t channelCount = colorType == PNG_COLOR_TYPE_RGB ? 3 : 4;
		int index = 0;
		for (png_uint_32 y = 0; y < height; ++y)
		{
			for (png_uint_32 x = 0; x < width; ++x)
			{
				for (size_t b = 0; b < channelCount; ++b)
				{
					out_data[index++] = rows[height - 1 - y][x * channelCount + b];
				}
			}
		}
		
		return true;
	}
}
