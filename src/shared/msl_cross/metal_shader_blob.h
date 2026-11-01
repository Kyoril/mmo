// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mmo
{
	/// @brief Layout of a Metal shader as stored in .hmat files (platform "METAL") and in the headers
	///        generated for built-in shaders: a fixed header followed by either Metal Shading Language
	///        source or a precompiled Metal library (.metallib).
	/// @remarks The Metal backend compiles source at load time and loads libraries directly. The entry
	///          point is always "main0".
	namespace metal_shader_blob
	{
		/// Identifies a source blob, followed by its format version in the last byte.
		constexpr char Magic[4] = { 'M', 'S', 'L', '1' };

		/// Identifies a precompiled library blob, followed by its format version in the last byte.
		constexpr char LibraryMagic[4] = { 'M', 'L', 'B', '1' };

		/// What follows the header.
		enum class Payload
		{
			/// Metal Shading Language source.
			Source,
			/// A compiled Metal library.
			Library
		};

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
			/// Number of payload bytes following the header (source without a terminating zero, or the library).
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

		/// @brief Packs a compiled Metal library into a blob.
		inline std::vector<uint8> PackLibrary(const Stage stage, const std::span<const uint8> metallib, const uint32 threadgroupX = 1, const uint32 threadgroupY = 1, const uint32 threadgroupZ = 1)
		{
			Header header {};
			std::memcpy(header.magic, LibraryMagic, sizeof(LibraryMagic));
			header.stage = stage;
			header.threadgroupSize[0] = threadgroupX;
			header.threadgroupSize[1] = threadgroupY;
			header.threadgroupSize[2] = threadgroupZ;
			header.sourceSize = static_cast<uint32>(metallib.size());

			std::vector<uint8> blob(sizeof(Header) + metallib.size());
			std::memcpy(blob.data(), &header, sizeof(Header));
			if (!metallib.empty())
			{
				std::memcpy(blob.data() + sizeof(Header), metallib.data(), metallib.size());
			}
			return blob;
		}

		/// @brief Reads the header and payload of a source or library blob.
		/// @returns false if the data is not a valid blob.
		inline bool Unpack(const void* data, const size_t size, Header& outHeader, Payload& outPayload, std::span<const uint8>& outBytes)
		{
			if (data == nullptr || size < sizeof(Header))
			{
				return false;
			}

			std::memcpy(&outHeader, data, sizeof(Header));
			if (std::memcmp(outHeader.magic, Magic, sizeof(Magic)) == 0)
			{
				outPayload = Payload::Source;
			}
			else if (std::memcmp(outHeader.magic, LibraryMagic, sizeof(LibraryMagic)) == 0)
			{
				outPayload = Payload::Library;
			}
			else
			{
				return false;
			}

			if (sizeof(Header) + static_cast<size_t>(outHeader.sourceSize) > size)
			{
				return false;
			}

			outBytes = std::span<const uint8>(static_cast<const uint8*>(data) + sizeof(Header), outHeader.sourceSize);
			return true;
		}

		/// @brief Reads the header and source of a source blob.
		/// @returns false if the data is not a valid source blob.
		inline bool Unpack(const void* data, const size_t size, Header& outHeader, std::string_view& outSource)
		{
			Payload payload {};
			std::span<const uint8> bytes;
			if (!Unpack(data, size, outHeader, payload, bytes) || payload != Payload::Source)
			{
				return false;
			}

			outSource = std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
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
