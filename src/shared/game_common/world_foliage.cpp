// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_foliage.h"

#include "base/chunk_writer.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const ChunkMagic FoliageVersionChunk = MakeChunkMagic('FVER');
	static const ChunkMagic FoliageMeshNamesChunk = MakeChunkMagic('FMSH');
	static const ChunkMagic FoliageInstanceChunk = MakeChunkMagic('FINS');

	WorldFoliageLoader::WorldFoliageLoader()
	{
		m_ignoreUnhandledChunks = true;

		AddChunkHandler(*FoliageVersionChunk, true, *this, &WorldFoliageLoader::OnVersionChunk);
	}

	bool WorldFoliageLoader::OnVersionChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		RemoveChunkHandler(*FoliageVersionChunk);

		if (!(reader >> io::read<uint32>(m_version)))
		{
			ELOG("Failed to read foliage version");
			return false;
		}

		if (m_version < foliage_version::Version_0_0_0_1)
		{
			ELOG("Unsupported foliage version " << m_version);
			return false;
		}

		AddChunkHandler(*FoliageMeshNamesChunk, false, *this, &WorldFoliageLoader::OnMeshNamesChunk);
		AddChunkHandler(*FoliageInstanceChunk, false, *this, &WorldFoliageLoader::OnInstanceChunk);
		return reader;
	}

	bool WorldFoliageLoader::OnMeshNamesChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		ASSERT(chunkHeader == *FoliageMeshNamesChunk);

		if (!m_meshNames.empty())
		{
			ELOG("Duplicate foliage mesh names chunk detected!");
			return false;
		}

		const size_t contentStart = reader.getSource()->position();
		while (reader.getSource()->position() - contentStart < chunkSize)
		{
			String meshName;
			if (!(reader >> io::read_container<uint16>(meshName)))
			{
				ELOG("Failed to read foliage mesh name: Unexpected end of file");
				return false;
			}

			m_meshNames.emplace_back(std::move(meshName));
		}

		return reader;
	}

	bool WorldFoliageLoader::OnInstanceChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		ASSERT(chunkHeader == *FoliageInstanceChunk);

		if (m_meshNames.empty())
		{
			ELOG("No foliage mesh names known, can't read instances before mesh chunk!");
			return false;
		}

		uint32 instanceCount = 0;
		if (!(reader >> io::read<uint32>(instanceCount)))
		{
			ELOG("Failed to read foliage instance count, unexpected end of file!");
			return false;
		}

		m_instances.reserve(m_instances.size() + instanceCount);

		for (uint32 i = 0; i < instanceCount; ++i)
		{
			uint64 uniqueId = 0;
			uint32 meshIndex = 0;
			Vector3 position;
			Quaternion rotation;
			Vector3 scale;

			if (!(reader
				>> io::read<uint64>(uniqueId)
				>> io::read<uint32>(meshIndex)
				>> io::read<float>(position.x)
				>> io::read<float>(position.y)
				>> io::read<float>(position.z)
				>> io::read<float>(rotation.w)
				>> io::read<float>(rotation.x)
				>> io::read<float>(rotation.y)
				>> io::read<float>(rotation.z)
				>> io::read<float>(scale.x)
				>> io::read<float>(scale.y)
				>> io::read<float>(scale.z)))
			{
				ELOG("Failed to read foliage instance, unexpected end of file!");
				return false;
			}

			if (meshIndex >= m_meshNames.size())
			{
				ELOG("Foliage instance references unknown mesh name!");
				return false;
			}

			ASSERT(position.IsValid());
			ASSERT(!rotation.IsNaN());
			ASSERT(scale.IsValid());

			FoliageInstance instance;
			instance.uniqueId = uniqueId;
			instance.meshName = m_meshNames[meshIndex];
			instance.position = position;
			instance.rotation = rotation;
			instance.scale = scale;
			m_instances.emplace_back(std::move(instance));
		}

		return reader;
	}

	void WorldFoliageSerializer::Write(io::Writer& writer, const std::vector<FoliageInstance>& instances)
	{
		// Version chunk.
		{
			ChunkWriter versionChunk(FoliageVersionChunk, writer);
			writer << io::write<uint32>(foliage_version::Version_0_0_0_1);
			versionChunk.Finish();
		}

		// Build a deduped mesh name table and remember each instance's index into it.
		std::vector<String> meshNames;
		std::vector<uint32> instanceMeshIndices;
		instanceMeshIndices.reserve(instances.size());

		for (const auto& instance : instances)
		{
			uint32 index = 0;
			bool found = false;
			for (uint32 i = 0; i < static_cast<uint32>(meshNames.size()); ++i)
			{
				if (meshNames[i] == instance.meshName)
				{
					index = i;
					found = true;
					break;
				}
			}

			if (!found)
			{
				index = static_cast<uint32>(meshNames.size());
				meshNames.push_back(instance.meshName);
			}

			instanceMeshIndices.push_back(index);
		}

		// Mesh names chunk.
		{
			ChunkWriter meshNamesChunk(FoliageMeshNamesChunk, writer);
			for (const auto& meshName : meshNames)
			{
				writer << io::write_dynamic_range<uint16>(meshName);
			}
			meshNamesChunk.Finish();
		}

		// Instance chunk.
		{
			ChunkWriter instanceChunk(FoliageInstanceChunk, writer);
			writer << io::write<uint32>(static_cast<uint32>(instances.size()));

			for (size_t i = 0; i < instances.size(); ++i)
			{
				const FoliageInstance& instance = instances[i];
				writer
					<< io::write<uint64>(instance.uniqueId)
					<< io::write<uint32>(instanceMeshIndices[i])
					<< io::write<float>(instance.position.x)
					<< io::write<float>(instance.position.y)
					<< io::write<float>(instance.position.z)
					<< io::write<float>(instance.rotation.w)
					<< io::write<float>(instance.rotation.x)
					<< io::write<float>(instance.rotation.y)
					<< io::write<float>(instance.rotation.z)
					<< io::write<float>(instance.scale.x)
					<< io::write<float>(instance.scale.y)
					<< io::write<float>(instance.scale.z);
			}

			instanceChunk.Finish();
		}
	}
}
