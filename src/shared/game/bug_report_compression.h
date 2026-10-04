// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <zlib.h>

#include <string>
#include <vector>

namespace mmo
{
	namespace game
	{
		/// Compresses a bug report's client JSON. Returns an empty vector on failure.
		inline std::vector<char> CompressBugReportData(const std::string& data)
		{
			uLongf compressedSize = compressBound(static_cast<uLong>(data.size()));
			std::vector<char> compressed(compressedSize);
			if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressedSize,
				reinterpret_cast<const Bytef*>(data.data()), static_cast<uLong>(data.size()), Z_BEST_COMPRESSION) != Z_OK)
			{
				return {};
			}

			compressed.resize(compressedSize);
			return compressed;
		}

		/// Inflates a bug report's client JSON. Fails if the data is corrupt, if it inflates to
		/// more than maxSize bytes (decompression bombs), or if the result does not match the size
		/// the sender claimed. The claimed size is never used to allocate more than maxSize.
		inline bool InflateBugReportData(const std::vector<char>& compressed, const uint32 claimedSize, const uint32 maxSize, std::string& out)
		{
			out.clear();
			if (claimedSize > maxSize)
			{
				return false;
			}
			if (claimedSize == 0)
			{
				return compressed.empty();
			}

			// One spare byte: if inflate fills it, the stream holds more than claimed.
			std::vector<char> buffer(static_cast<size_t>(claimedSize) + 1);

			z_stream stream{};
			stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
			stream.avail_in = static_cast<uInt>(compressed.size());
			stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
			stream.avail_out = static_cast<uInt>(buffer.size());

			if (inflateInit(&stream) != Z_OK)
			{
				return false;
			}

			const int result = inflate(&stream, Z_FINISH);
			const uLong produced = stream.total_out;
			inflateEnd(&stream);

			if (result != Z_STREAM_END || produced != claimedSize)
			{
				return false;
			}

			out.assign(buffer.data(), produced);
			return true;
		}
	}
}
