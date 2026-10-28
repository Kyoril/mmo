// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "png_writer.h"

#include <zlib.h>

#include <fstream>
#include <system_error>

namespace mmo
{
	namespace tex
	{
		namespace
		{
			void AppendBigEndian(std::vector<uint8>& out, const uint32 value)
			{
				out.push_back(static_cast<uint8>(value >> 24));
				out.push_back(static_cast<uint8>(value >> 16));
				out.push_back(static_cast<uint8>(value >> 8));
				out.push_back(static_cast<uint8>(value));
			}

			void AppendChunk(std::vector<uint8>& out, const char type[4], const uint8* data, const uint32 size)
			{
				AppendBigEndian(out, size);

				const size_t typeOffset = out.size();
				out.insert(out.end(), type, type + 4);
				if (size > 0)
				{
					out.insert(out.end(), data, data + size);
				}

				// The CRC covers the chunk type and its data, not the length.
				const uLong crc = crc32(0L, out.data() + typeOffset, static_cast<uInt>(4 + size));
				AppendBigEndian(out, static_cast<uint32>(crc));
			}
		}

		std::vector<uint8> EncodePng(const uint32 width, const uint32 height, const uint8* rgba, const bool keepAlpha)
		{
			if (width == 0 || height == 0 || rgba == nullptr)
			{
				return {};
			}

			const uint32 channels = keepAlpha ? 4 : 3;
			const size_t rowSize = static_cast<size_t>(width) * channels;

			// Every row starts with its filter type. Filter 0 (none) keeps this simple; zlib still
			// shrinks a rendered frame well.
			std::vector<uint8> raw;
			raw.reserve((rowSize + 1) * height);
			for (uint32 y = 0; y < height; ++y)
			{
				raw.push_back(0);
				const uint8* row = rgba + static_cast<size_t>(y) * width * 4;
				for (uint32 x = 0; x < width; ++x)
				{
					raw.insert(raw.end(), row + x * 4, row + x * 4 + channels);
				}
			}

			uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
			std::vector<uint8> compressed(compressedSize);
			if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK)
			{
				return {};
			}
			compressed.resize(compressedSize);

			std::vector<uint8> png;
			png.reserve(compressed.size() + 64);

			static const uint8 signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
			png.insert(png.end(), signature, signature + 8);

			std::vector<uint8> header;
			AppendBigEndian(header, width);
			AppendBigEndian(header, height);
			header.push_back(8);						// bit depth
			header.push_back(keepAlpha ? 6 : 2);		// colour type: RGBA or RGB
			header.push_back(0);						// compression: deflate
			header.push_back(0);						// filter method
			header.push_back(0);						// no interlace
			AppendChunk(png, "IHDR", header.data(), static_cast<uint32>(header.size()));
			AppendChunk(png, "IDAT", compressed.data(), static_cast<uint32>(compressed.size()));
			AppendChunk(png, "IEND", nullptr, 0);

			return png;
		}

		bool WritePng(const std::filesystem::path& path, const uint32 width, const uint32 height, const uint8* rgba, const bool keepAlpha)
		{
			const std::vector<uint8> png = EncodePng(width, height, rgba, keepAlpha);
			if (png.empty())
			{
				return false;
			}

			if (path.has_parent_path())
			{
				std::error_code error;
				std::filesystem::create_directories(path.parent_path(), error);
			}

			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				return false;
			}

			file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
			return static_cast<bool>(file);
		}
	}
}
