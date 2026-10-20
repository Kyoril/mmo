// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/chunk_writer.h"
#include "base/typedefs.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_serializer.h"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace mmo;

namespace
{
	bool ReadShippedFile(const String& path, std::vector<char>& out)
	{
		std::ifstream file(MMO_SOURCE_DIR "/data/client/" + path, std::ios::binary);
		if (!file)
		{
			return false;
		}

		out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return true;
	}

	/// Sound entry ids in a shipped mesh's SNDS chunk, read straight from the file: a full mesh
	/// load would need the mesh's real materials, which a headless test cannot compile.
	std::vector<uint32> ShippedMeshSoundIds(const String& path)
	{
		std::vector<char> buffer;
		std::vector<uint32> ids;
		if (!ReadShippedFile(path, buffer))
		{
			return ids;
		}

		const uint32 soundsMagic = *MakeChunkMagic('SNDS');
		size_t offset = 0;
		while (offset + 8 <= buffer.size())
		{
			uint32 magic = 0;
			uint32 size = 0;
			std::memcpy(&magic, &buffer[offset], 4);
			std::memcpy(&size, &buffer[offset + 4], 4);
			const size_t body = offset + 8;
			if (magic == soundsMagic && body + 3 <= buffer.size() && buffer[body] == 1)
			{
				uint16 count = 0;
				std::memcpy(&count, &buffer[body + 1], 2);
				for (uint16 i = 0; i < count && body + 3 + (i + 1) * 24 <= buffer.size(); ++i)
				{
					uint32 id = 0;
					std::memcpy(&id, &buffer[body + 3 + i * 24], 4);
					ids.push_back(id);
				}
			}
			offset = body + size;
		}
		return ids;
	}
}

TEST_CASE("Shipped fire and portal meshes carry their ambient sounds", "[mesh_sounds][content]")
{
	std::vector<char> probe;
	if (!ReadShippedFile("Models/Dungeon/Props/Torch_01.hmsh", probe))
	{
		WARN("data/client is not checked out, skipping");
		return;
	}

	CHECK(ShippedMeshSoundIds("Models/Dungeon/Props/Torch_01.hmsh") == std::vector<uint32>{ 300 });
	CHECK(ShippedMeshSoundIds("Models/FalwynPlains/Props/Camp/SM_hc_CampFire_A.hmsh") == std::vector<uint32>{ 301 });
	CHECK(ShippedMeshSoundIds("Models/FalwynPlains/Props/Furniture/SM_hc_Brazier_A.hmsh") == std::vector<uint32>{ 301 });
	CHECK(ShippedMeshSoundIds("Models/FalwynPlains/Props/Furniture/SM_hc_Brazier_B.hmsh") == std::vector<uint32>{ 301 });
	CHECK(ShippedMeshSoundIds("Models/Dungeon/DungeonPortal_01.hmsh") == std::vector<uint32>{ 302 });
}

TEST_CASE("The Hollow Choir monastery places the torch mesh that carries the flame sound", "[mesh_sounds][content]")
{
	std::vector<char> buffer;
	if (!ReadShippedFile("Models/Dungeon/Monastery_001.hwmo", buffer))
	{
		WARN("data/client is not checked out, skipping");
		return;
	}

	WorldModel model("Monastery_001");
	{
		io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
		io::Reader reader{ source };
		WorldModelDeserializer deserializer{ model };
		REQUIRE(deserializer.Read(reader));
	}

	const String torchPath = "Models/Dungeon/Props/Torch_01.hmsh";
	size_t torchCount = 0;
	for (size_t i = 0; i < model.GetGroupCount(); ++i)
	{
		for (const WorldModelMeshRef& ref : model.GetGroup(i)->GetMeshRefs())
		{
			torchCount += ref.visible && ref.meshPath == torchPath ? 1 : 0;
		}
	}
	for (const String& name : model.GetDoodadNames())
	{
		torchCount += name == torchPath ? 1 : 0;
	}

	CHECK(torchCount > 0);
}
