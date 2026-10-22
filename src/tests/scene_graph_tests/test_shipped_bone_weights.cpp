// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/chunk_writer.h"
#include "base/typedefs.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

using namespace mmo;

namespace
{
	/// The loader keeps this many weights per vertex and trims the rest on every load.
	constexpr uint32 MaxBlendWeights = 4;

	/// Chunk magic as it appears in the file. MESH and SUBM are written byte by byte, unlike the
	/// MakeChunkMagic('BONE') chunks, whose multi-char literal comes out reversed.
	uint32 FileMagic(const char (&text)[5])
	{
		uint32 magic = 0;
		std::memcpy(&magic, text, 4);
		return magic;
	}

	/// Byte reader over a whole file that fails softly instead of reading past the end.
	struct Cursor
	{
		const std::vector<char>& data;
		size_t offset;
		bool ok = true;

		template<typename T>
		T Read()
		{
			T value{};
			if (!ok || offset + sizeof(T) > data.size())
			{
				ok = false;
				return value;
			}

			std::memcpy(&value, &data[offset], sizeof(T));
			offset += sizeof(T);
			return value;
		}

		void Skip(const size_t bytes)
		{
			if (!ok || offset + bytes > data.size())
			{
				ok = false;
				return;
			}

			offset += bytes;
		}
	};

	/// Most bone weights any vertex of the mesh has, read straight from the file: a full mesh load
	/// would need the mesh's real materials, which a headless test cannot compile. Counts both the
	/// shared-vertex BONE chunks and the per-submesh assignments of SUBM chunks (format 0x0300+).
	/// Also reports how many assignments were read and whether the mesh links a skeleton, so a
	/// parser that silently skips the chunks cannot pass. Returns false if the file cannot be read
	/// or parsed.
	bool MaxBoneWeightsPerVertex(const std::filesystem::path& path, uint32& out_max, size_t& out_assignments, bool& out_skinned)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return false;
		}

		const std::vector<char> buffer{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

		const uint32 meshMagic = FileMagic("MESH");
		const uint32 boneMagic = *MakeChunkMagic('BONE');
		const uint32 subMeshMagic = FileMagic("SUBM");
		const uint32 skeletonMagic = *MakeChunkMagic('SKEL');

		uint32 version = 0;
		std::unordered_map<uint32, uint32> sharedCounts;
		out_max = 0;
		out_assignments = 0;
		out_skinned = false;

		size_t offset = 0;
		while (offset + 8 <= buffer.size())
		{
			uint32 magic = 0;
			uint32 size = 0;
			std::memcpy(&magic, &buffer[offset], 4);
			std::memcpy(&size, &buffer[offset + 4], 4);
			const size_t body = offset + 8;
			if (body + size > buffer.size())
			{
				return false;
			}

			Cursor cursor{ buffer, body };
			if (magic == meshMagic)
			{
				version = cursor.Read<uint32>();
			}
			else if (magic == skeletonMagic)
			{
				out_skinned = true;
			}
			else if (magic == boneMagic)
			{
				const uint32 vertexIndex = cursor.Read<uint32>();
				out_max = std::max(out_max, ++sharedCounts[vertexIndex]);
				++out_assignments;
			}
			else if (magic == subMeshMagic && version >= 0x0300)
			{
				cursor.Skip(cursor.Read<uint8>());
				cursor.Skip(cursor.Read<uint16>());
				const bool useSharedVertices = cursor.Read<uint8>() != 0;
				if (version >= 0x0301)
				{
					cursor.Skip(1);
				}

				if (!useSharedVertices)
				{
					// Position, color, uvw, normal, binormal, tangent: 16 four-byte values.
					cursor.Skip(static_cast<size_t>(cursor.Read<uint32>()) * 64);
				}

				if (cursor.Read<uint8>() != 0)
				{
					const uint32 indexCount = cursor.Read<uint32>();
					const uint8 indexSize = cursor.Read<uint8>();
					cursor.Skip(static_cast<size_t>(indexCount) * (indexSize == 0 ? 2 : 4));
				}

				std::unordered_map<uint32, uint32> counts;
				const uint32 assignmentCount = cursor.Read<uint32>();
				out_assignments += assignmentCount;
				for (uint32 i = 0; i < assignmentCount && cursor.ok; ++i)
				{
					const uint32 vertexIndex = cursor.Read<uint32>();
					cursor.Skip(sizeof(uint16) + sizeof(float));
					out_max = std::max(out_max, ++counts[vertexIndex]);
				}

				if (!cursor.ok || cursor.offset != body + size)
				{
					return false;
				}
			}

			if (!cursor.ok)
			{
				return false;
			}

			offset = body + size;
		}

		return true;
	}
}

TEST_CASE("Shipped character meshes have at most four bone weights per vertex", "[bone_weights][content]")
{
	// Extra weights are trimmed and renormalized on every load, which cost a ~100 ms main-thread
	// hitch per HumanMale spawn. Fix offenders with `mmo_edit --resave-mesh <asset.hmsh>`.
	const std::filesystem::path root = MMO_SOURCE_DIR "/data/client/Models/Character";
	if (!std::filesystem::exists(root))
	{
		WARN("data/client is not checked out, skipping");
		return;
	}

	size_t meshCount = 0;
	size_t skinnedCount = 0;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
	{
		if (!entry.is_regular_file() || entry.path().extension() != ".hmsh")
		{
			continue;
		}

		++meshCount;
		uint32 maxWeights = 0;
		size_t assignments = 0;
		bool skinned = false;
		INFO(entry.path().string());
		REQUIRE(MaxBoneWeightsPerVertex(entry.path(), maxWeights, assignments, skinned));
		CHECK(maxWeights <= MaxBlendWeights);
		if (skinned)
		{
			++skinnedCount;
			CHECK(assignments > 0);
		}
	}

	CHECK(meshCount > 0);
	CHECK(skinnedCount > 0);
}
