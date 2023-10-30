
#include "world_deserializer.h"

#include "base/chunk_writer.h"
#include "game_states/login_state.h"
#include "log/default_log_levels.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/scene.h"

namespace mmo
{
	static const ChunkMagic VersionChunkMagic = MakeChunkMagic('MVER');
	static const ChunkMagic MeshNamesChunkMagic = MakeChunkMagic('MESH');
	static const ChunkMagic EntityChunkMagic = MakeChunkMagic('MENT');

	ClientWorldInstanceDeserializer::ClientWorldInstanceDeserializer(ClientWorldInstance& world)
		: ChunkReader()
		, m_world(world)
	{
		AddChunkHandler(*VersionChunkMagic, true, *this, &ClientWorldInstanceDeserializer::ReadVersionChunk);
	}

	ClientWorldInstance::ClientWorldInstance(Scene& scene, SceneNode& rootNode)
		: m_scene(scene)
		, m_rootNode(rootNode)
	{
	}

	void ClientWorldInstance::CreateMapEntity(const String& meshName, const Vector3& position, const Quaternion& orientation, const Vector3& scale)
	{
		// Create scene node
		SceneNode* node = m_rootNode.CreateChildSceneNode();
		node->SetPosition(position);
		node->SetOrientation(orientation);
		node->SetScale(scale);

		// Create entity
		Entity* entity = m_scene.CreateEntity("Entity_" + std::to_string(m_entityIdGenerator.GenerateId()), meshName);
		node->AttachObject(*entity);
	}

	bool ClientWorldInstanceDeserializer::ReadVersionChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		// Read version
		uint32 version = 0;
		reader >> io::read<uint32>(version);

		// Check if version is supported
		if (version == world_version::Version_0_0_0_1)
		{
			AddChunkHandler(*MeshNamesChunkMagic, true, *this, &ClientWorldInstanceDeserializer::ReadMeshNamesChunk);
			AddChunkHandler(*EntityChunkMagic, false, *this, &ClientWorldInstanceDeserializer::ReadEntityChunk);
			return true;
		}

		ELOG("Unsupported world version: " << version);
		return false;
	}

	bool ClientWorldInstanceDeserializer::ReadMeshNamesChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		if (!m_meshNames.empty())
		{
			ELOG("Duplicate mesh names chunk detected!");
			return false;
		}

		const size_t contentStart = reader.getSource()->position();
		while (reader.getSource()->position() - contentStart < chunkSize)
		{
			String meshName;
			if (!(reader >> io::read_string(meshName)))
			{
				ELOG("Failed to read world file: Unexpected end of file");
				return false;
			}

			m_meshNames.emplace_back(std::move(meshName));
		}

		return true;
	}

	bool ClientWorldInstanceDeserializer::ReadEntityChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		if (m_meshNames.empty())
		{
			ELOG("No mesh names known, can't read entity chunks before mesh chunk!");
			return false;
		}

		if (chunkSize != sizeof(MapEntityChunkContent))
		{
			ELOG("Entity chunk has incorrect chunk size, found " << log_hex_digit(chunkSize) << " bytes, expected " << log_hex_digit(sizeof(MapEntityChunkContent)) << " bytes");
			return false;
		}

		MapEntityChunkContent content;
		reader.readPOD(content);
		if (!reader)
		{
			ELOG("Failed to read map entity chunk content, unexpected end of file!");
			return false;
		}

		if (content.meshNameIndex >= m_meshNames.size())
		{
			ELOG("Map entity chunk references unknown mesh names!");
			return false;
		}

		m_world.CreateMapEntity(m_meshNames[content.meshNameIndex], content.position, content.rotation, content.scale);
		return true;
	}
}
