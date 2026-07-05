// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "image_io.h"

#include "log/default_log_levels.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO_SECURE
#include "stb_image/stb_image.h"

#include <zlib.h>

#include <cstring>
#include <fstream>

namespace mmo
{
	namespace
	{
		void AppendU32BE(std::vector<uint8> &buffer, const uint32 value)
		{
			buffer.push_back(static_cast<uint8>((value >> 24) & 0xFF));
			buffer.push_back(static_cast<uint8>((value >> 16) & 0xFF));
			buffer.push_back(static_cast<uint8>((value >> 8) & 0xFF));
			buffer.push_back(static_cast<uint8>(value & 0xFF));
		}

		void AppendChunk(std::vector<uint8> &buffer, const char type[4], const uint8 *data, const size_t size)
		{
			AppendU32BE(buffer, static_cast<uint32>(size));

			const size_t typeOffset = buffer.size();
			buffer.insert(buffer.end(), type, type + 4);
			if (size > 0)
			{
				buffer.insert(buffer.end(), data, data + size);
			}

			const uLong crc = crc32(0L, buffer.data() + typeOffset, static_cast<uInt>(4 + size));
			AppendU32BE(buffer, static_cast<uint32>(crc));
		}

		/// @brief Writes a PNG file with filter type 0 on every scanline.
		///	@param colorType 0 = grayscale, 2 = rgb (per the PNG specification).
		bool WritePng(const std::filesystem::path &path, const uint32 width, const uint32 height,
			const uint8 bitDepth, const uint8 colorType, const std::vector<uint8> &rawScanlines, const size_t bytesPerRow)
		{
			// Prepend the filter byte (0 = None) to each scanline
			std::vector<uint8> filtered;
			filtered.reserve((bytesPerRow + 1) * height);
			for (uint32 row = 0; row < height; ++row)
			{
				filtered.push_back(0);
				const uint8 *rowData = rawScanlines.data() + row * bytesPerRow;
				filtered.insert(filtered.end(), rowData, rowData + bytesPerRow);
			}

			uLongf compressedSize = compressBound(static_cast<uLong>(filtered.size()));
			std::vector<uint8> compressed(compressedSize);
			if (compress2(compressed.data(), &compressedSize, filtered.data(), static_cast<uLong>(filtered.size()), 6) != Z_OK)
			{
				ELOG("Failed to compress PNG image data!");
				return false;
			}
			compressed.resize(compressedSize);

			std::vector<uint8> file;
			file.reserve(compressed.size() + 128);

			// PNG signature
			static const uint8 signature[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
			file.insert(file.end(), signature, signature + sizeof(signature));

			// IHDR
			std::vector<uint8> ihdr;
			AppendU32BE(ihdr, width);
			AppendU32BE(ihdr, height);
			ihdr.push_back(bitDepth);
			ihdr.push_back(colorType);
			ihdr.push_back(0);	// compression
			ihdr.push_back(0);	// filter
			ihdr.push_back(0);	// interlace
			AppendChunk(file, "IHDR", ihdr.data(), ihdr.size());

			// IDAT + IEND
			AppendChunk(file, "IDAT", compressed.data(), compressed.size());
			AppendChunk(file, "IEND", nullptr, 0);

			std::ofstream out{ path, std::ios::binary };
			if (!out.is_open())
			{
				ELOG("Failed to create PNG file '" << path.string() << "'!");
				return false;
			}

			out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
			return static_cast<bool>(out);
		}
	}

	bool LoadGray16Png(const std::filesystem::path &path, GrayImage16 &out)
	{
		int width = 0, height = 0, channels = 0;
		stbi_us *pixels = stbi_load_16(path.string().c_str(), &width, &height, &channels, 1);
		if (!pixels)
		{
			ELOG("Failed to load heightmap image '" << path.string() << "': " << stbi_failure_reason());
			return false;
		}

		out.width = static_cast<uint32>(width);
		out.height = static_cast<uint32>(height);
		out.pixels.assign(pixels, pixels + (static_cast<size_t>(width) * static_cast<size_t>(height)));
		stbi_image_free(pixels);

		return true;
	}

	bool SaveGray16Png(const std::filesystem::path &path, const GrayImage16 &image)
	{
		if (image.pixels.size() != static_cast<size_t>(image.width) * image.height)
		{
			ELOG("Grayscale image dimensions don't match the pixel buffer size!");
			return false;
		}

		// PNG stores 16-bit samples big-endian
		const size_t bytesPerRow = static_cast<size_t>(image.width) * 2;
		std::vector<uint8> raw(bytesPerRow * image.height);
		for (size_t i = 0; i < image.pixels.size(); ++i)
		{
			raw[i * 2] = static_cast<uint8>(image.pixels[i] >> 8);
			raw[i * 2 + 1] = static_cast<uint8>(image.pixels[i] & 0xFF);
		}

		return WritePng(path, image.width, image.height, 16, 0, raw, bytesPerRow);
	}

	bool SaveRgb8Png(const std::filesystem::path &path, const RgbImage8 &image)
	{
		if (image.pixels.size() != static_cast<size_t>(image.width) * image.height * 3)
		{
			ELOG("RGB image dimensions don't match the pixel buffer size!");
			return false;
		}

		const size_t bytesPerRow = static_cast<size_t>(image.width) * 3;
		return WritePng(path, image.width, image.height, 8, 2, image.pixels, bytesPerRow);
	}
}
