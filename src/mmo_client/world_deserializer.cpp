
#include "world_deserializer.h"

#include "base/chunk_writer.h"
#include "game_states/login_state.h"
#include "log/default_log_levels.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/scene.h"

namespace mmo
{
	static const ChunkMagic VersionChunkMagic = MakeChunkMagic('MVER');
	static const ChunkMagic MeshNamesChunkMagic = MakeChunkMagic('MESH');
	static const ChunkMagic EntityChunkMagic = MakeChunkMagic('MENT');
	static const ChunkMagic TerrainChunkMagic = MakeChunkMagic('RRET');

	ClientWorldInstanceDeserializer::ClientWorldInstanceDeserializer(ClientWorldInstance& world)
		: ChunkReader()
		, m_world(world)
	{
		AddChunkHandler(*VersionChunkMagic, true, *this, &ClientWorldInstanceDeserializer::ReadVersionChunk);
	}

	ClientWorldInstance::ClientWorldInstance(Scene& scene, SceneNode& rootNode, const String& name)
		: m_scene(scene)
		, m_rootNode(rootNode)
		, m_name(name)
	{
	}

	Entity* ClientWorldInstance::CreateMapEntity(const String& meshName, const Vector3& position, const Quaternion& orientation, const Vector3& scale)
	{
		// Create scene node
		SceneNode* node = m_rootNode.CreateChildSceneNode();
		node->SetPosition(position);
		node->SetOrientation(orientation);
		node->SetScale(scale);

		// Create entity
		Entity* entity = m_scene.CreateEntity("Entity_" + std::to_string(m_entityIdGenerator.GenerateId()), meshName);
		node->AttachObject(*entity);
		entity->SetQueryFlags(1);

		return entity;
	}

	bool ClientWorldInstanceDeserializer::ReadVersionChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		ASSERT(chunkHeader == *VersionChunkMagic);

		// Read version
		m_version = 0;
		reader >> io::read<uint32>(m_version);

		// Check if version is supported
		if (m_version >= world_version::Version_0_0_0_1)
		{
			AddChunkHandler(*MeshNamesChunkMagic, false, *this, &ClientWorldInstanceDeserializer::ReadMeshNamesChunk);
			AddChunkHandler(*TerrainChunkMagic, false, *this, &ClientWorldInstanceDeserializer::ReadTerrainChunk);
			return true;
		}

		ELOG("Unsupported world version: " << m_version);
		return false;
	}

	bool ClientWorldInstanceDeserializer::ReadMeshNamesChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		ASSERT(chunkHeader == *MeshNamesChunkMagic);

		RemoveChunkHandler(*MeshNamesChunkMagic);

		if (m_version == world_version::Version_0_0_0_1)
		{
			AddChunkHandler(*EntityChunkMagic, false, *this, &ClientWorldInstanceDeserializer::ReadEntityChunk);
		}
		else if (m_version >= world_version::Version_0_0_0_2)
		{
			AddChunkHandler(*EntityChunkMagic, false, *this, &ClientWorldInstanceDeserializer::ReadEntityChunkV2);
		}

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

		return reader;
	}

	bool ClientWorldInstanceDeserializer::ReadEntityChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		ASSERT(chunkHeader == *EntityChunkMagic);

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
		return reader;
	}

	bool ClientWorldInstanceDeserializer::ReadEntityChunkV2(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *EntityChunkMagic);

		if (m_meshNames.empty())
		{
			ELOG("No mesh names known, can't read entity chunks before mesh chunk!");
			return false;
		}

		uint32 uniqueId;
		uint32 meshNameIndex;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale;
		if (!(reader 
			>> io::read<uint32>(uniqueId) 
			>> io::read<uint32>(meshNameIndex) 
			>> io::read<float>(position.x)
			>> io::read<float>(position.y)
			>> io::read<float>(position.z)
			>> io::read<float>(rotation.w)
			>> io::read<float>(rotation.x)
			>> io::read<float>(rotation.y)
			>> io::read<float>(rotation.z)
			>> io::read<float>(scale.x)
			>> io::read<float>(scale.y)
			>> io::read<float>(scale.z)
			))
		{
			ELOG("Failed to read map entity chunk content, unexpected end of file!");
			return false;
		}

		ASSERT(!position.IsValid());
		ASSERT(!rotation.IsNaN());
		ASSERT(!scale.IsValid());

		if (meshNameIndex >= m_meshNames.size())
		{
			ELOG("Map entity chunk references unknown mesh names!");
			return false;
		}

		struct MaterialOverride
		{
			uint8 materialIndex;
			String materialName;
		};

		std::vector<MaterialOverride> materialOverrides;

		uint8 numMaterialOverrides;
		if (!(reader >> io::read<uint8>(numMaterialOverrides)))
		{
			ELOG("Failed to read material override count for map entity chunk, unexpected end of file!");
			return false;
		}

		materialOverrides.resize(numMaterialOverrides);
		for (uint8 i = 0; i < numMaterialOverrides; ++i)
		{
			if (!(reader >> io::read<uint8>(materialOverrides[i].materialIndex) >> io::read_container<uint16>(materialOverrides[i].materialName)))
			{
				ELOG("Failed to read material override for map entity chunk, unexpected end of file!");
				return false;
			}
		}

		if (Entity* entity = m_world.CreateMapEntity(m_meshNames[meshNameIndex], position, rotation, scale))
		{
			// Apply material overrides
			for (const auto& materialOverride : materialOverrides)
			{
				if (materialOverride.materialIndex >= entity->GetNumSubEntities())
				{
					WLOG("Entity has material override for material index greater than entity material count! Skipping material override");
					continue;
				}

				entity->GetSubEntity(materialOverride.materialIndex)->SetMaterial(MaterialManager::Get().Load(materialOverride.materialName));
			}
		}

		return reader;
	}

	bool ClientWorldInstanceDeserializer::ReadTerrainChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *TerrainChunkMagic);

		uint8 hasTerrain;
		if (!(reader >> io::read<uint8>(hasTerrain)))
		{
			ELOG("Failed to read terrain chunk: Unexpected end of file");
			return false;
		}

		if (hasTerrain)
		{
			// Ensure terrain is created
			m_world.m_terrain = std::make_unique<terrain::Terrain>(m_world.m_scene, nullptr, 64, 64);
			m_world.m_terrain->SetBaseFileName(m_world.m_name);
		}

		// Read terrain default material
		String defaultMaterialName;
		if (!(reader >> io::read_container<uint16>(defaultMaterialName)))
		{
			ELOG("Failed to read terrain default material name: Unexpected end of file");
			return false;
		}

		if (hasTerrain)
		{
			m_world.GetTerrain()->SetDefaultMaterial(MaterialManager::Get().Load(defaultMaterialName));
		}

		return reader;
	}
}
