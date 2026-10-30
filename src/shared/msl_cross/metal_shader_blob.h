// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace mmo
{
	/// @brief Layout of a Metal shader as stored in .hmat files (platform "METAL") and in the headers
	///        generated for built-in shaders: a fixed header followed by Metal Shading Language source.
	/// @remarks The Metal backend compiles the source at load time. The entry point is always "main0".
	namespace metal_shader_blob
	{
		/// Identifies a blob, followed by its format version in the last byte.
		constexpr char Magic[4] = { 'M', 'S', 'L', '1' };

		/// Stage of the shader in the blob.
		enum class Stage : uint32
		{
			Vertex = 0,
			Pixel = 1,
			Compute = 2
		};

		/// Fixed size header in front of the source.
		struct Header
		{
			char magic[4];
			Stage stage;
			/// Threads per threadgroup of a compute shader ([numthreads] in HLSL). Unused otherwise.
			uint32 threadgroupSize[3];
			/// Number of source bytes following the header, without a terminating zero.
			uint32 sourceSize;
		};

		static_assert(sizeof(Header) == 24, "Metal shader blob header layout must not change");

		/// @brief Packs Metal source into a blob.
		inline std::vector<uint8> Pack(const Stage stage, const std::string_view source, const uint32 threadgroupX = 1, const uint32 threadgroupY = 1, const uint32 threadgroupZ = 1)
		{
			Header header {};
			std::memcpy(header.magic, Magic, sizeof(Magic));
			header.stage = stage;
			header.threadgroupSize[0] = threadgroupX;
			header.threadgroupSize[1] = threadgroupY;
			header.threadgroupSize[2] = threadgroupZ;
			header.sourceSize = static_cast<uint32>(source.size());

			std::vector<uint8> blob(sizeof(Header) + source.size());
			std::memcpy(blob.data(), &header, sizeof(Header));
			std::memcpy(blob.data() + sizeof(Header), source.data(), source.size());
			return blob;
		}

		/// @brief Reads the header and source of a blob.
		/// @returns false if the data is not a valid blob.
		inline bool Unpack(const void* data, const size_t size, Header& outHeader, std::string_view& outSource)
		{
			if (data == nullptr || size < sizeof(Header))
			{
				return false;
			}

			std::memcpy(&outHeader, data, sizeof(Header));
			if (std::memcmp(outHeader.magic, Magic, sizeof(Magic)) != 0 || sizeof(Header) + outHeader.sourceSize > size)
			{
				return false;
			}

			outSource = std::string_view(static_cast<const char*>(data) + sizeof(Header), outHeader.sourceSize);
			return true;
		}
	}

	/// @brief Resource index layout shared by the HLSL to Metal cross compiler and the Metal backend.
	/// @remarks HLSL registers keep their number where Metal has a matching argument table: cbuffer bN
	///          becomes buffer(N), Texture tN becomes texture(N), sampler sN becomes sampler(N). The
	///          remaining kinds are moved into ranges the others do not use.
	namespace metal_bindings
	{
		/// buffer(index) of a StructuredBuffer at register tN (read-only, any stage).
		constexpr uint32 StructuredBufferBase = 14;

		/// buffer(index) of a RWStructuredBuffer at register uN.
		constexpr uint32 RWStructuredBufferBase = 25;

		/// texture(index) of a RWTexture at register uN.
		constexpr uint32 RWTextureBase = 16;

		/// buffer(index) of vertex stream N is VertexBufferTop - N.
		constexpr uint32 VertexBufferTop = 30;

		/// Highest number of vertex streams a draw can use.
		constexpr uint32 MaxVertexStreams = 4;

		/// Number of constant buffer slots (b0 to b13).
		constexpr uint32 MaxConstantBuffers = StructuredBufferBase;

		/// Number of sampler slots Metal offers per stage.
		constexpr uint32 MaxSamplers = 16;
	}
}
