// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_fog_volumes.h"

#include "base/chunk_writer.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	// Chosen to avoid colliding with the foliage file's own 'FVER'/'FMSH'/'FINS' chunks, since
	// both formats can in principle live in the same reader/tool ecosystem.
	static const ChunkMagic FogVolumeVersionChunk = MakeChunkMagic('FGVR');
	static const ChunkMagic FogVolumeDataChunk = MakeChunkMagic('FGVL');

	WorldFogVolumeDeserializer::WorldFogVolumeDeserializer(std::vector<FogVolume>& out)
		: ChunkReader(true)
		, m_volumes(out)
	{
		AddChunkHandler(*FogVolumeVersionChunk, true, *this, &WorldFogVolumeDeserializer::OnVersionChunk);
	}

	bool WorldFogVolumeDeserializer::OnVersionChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		RemoveChunkHandler(*FogVolumeVersionChunk);

		if (!(reader >> io::read<uint32>(m_version)))
		{
			ELOG("Failed to read fog volume version");
			return false;
		}

		if (m_version > fog_volume_version::Version_0_0_0_1)
		{
			ELOG("Unsupported fog volume version " << m_version);
			return false;
		}

		AddChunkHandler(*FogVolumeDataChunk, false, *this, &WorldFogVolumeDeserializer::OnVolumesChunk);
		return reader;
	}

	bool WorldFogVolumeDeserializer::OnVolumesChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		ASSERT(chunkHeader == *FogVolumeDataChunk);

		uint32 volumeCount = 0;
		if (!(reader >> io::read<uint32>(volumeCount)))
		{
			ELOG("Failed to read fog volume count, unexpected end of file!");
			return false;
		}

		m_volumes.reserve(m_volumes.size() + volumeCount);

		for (uint32 i = 0; i < volumeCount; ++i)
		{
			FogVolume volume;
			uint8 shape = 0;
			uint8 noiseDetail = 0;

			if (!(reader
				>> io::read<uint32>(volume.id)
				>> io::read_container<uint16>(volume.name)
				>> io::read<uint8>(shape)
				>> io::read<float>(volume.position.x)
				>> io::read<float>(volume.position.y)
				>> io::read<float>(volume.position.z)
				>> io::read<float>(volume.size.x)
				>> io::read<float>(volume.size.y)
				>> io::read<float>(volume.size.z)
				>> io::read<float>(volume.yaw)
				>> io::read<float>(volume.density)
				>> io::read<float>(volume.color.x)
				>> io::read<float>(volume.color.y)
				>> io::read<float>(volume.color.z)
				>> io::read<float>(volume.edgeFade)
				>> io::read<float>(volume.heightFalloff)
				>> io::read<float>(volume.activeFrom)
				>> io::read<float>(volume.activeTo)
				>> io::read<float>(volume.fadeHours)
				>> io::read<float>(volume.noiseAmount)
				>> io::read<uint8>(noiseDetail)))
			{
				ELOG("Failed to read fog volume, unexpected end of file!");
				return false;
			}

			volume.shape = static_cast<FogVolumeShape>(shape);
			volume.noiseDetail = noiseDetail;

			SanitizeFogVolume(volume);
			m_volumes.emplace_back(std::move(volume));
		}

		return reader;
	}

	void WorldFogVolumeSerializer::Write(io::Writer& writer, const std::vector<FogVolume>& volumes)
	{
		// Version chunk.
		{
			ChunkWriter versionChunk(FogVolumeVersionChunk, writer);
			writer << io::write<uint32>(fog_volume_version::Version_0_0_0_1);
			versionChunk.Finish();
		}

		// Volume data chunk.
		{
			ChunkWriter volumesChunk(FogVolumeDataChunk, writer);
			writer << io::write<uint32>(static_cast<uint32>(volumes.size()));

			for (const auto& volume : volumes)
			{
				writer
					<< io::write<uint32>(volume.id)
					<< io::write_dynamic_range<uint16>(volume.name)
					<< io::write<uint8>(static_cast<uint8>(volume.shape))
					<< io::write<float>(volume.position.x)
					<< io::write<float>(volume.position.y)
					<< io::write<float>(volume.position.z)
					<< io::write<float>(volume.size.x)
					<< io::write<float>(volume.size.y)
					<< io::write<float>(volume.size.z)
					<< io::write<float>(volume.yaw)
					<< io::write<float>(volume.density)
					<< io::write<float>(volume.color.x)
					<< io::write<float>(volume.color.y)
					<< io::write<float>(volume.color.z)
					<< io::write<float>(volume.edgeFade)
					<< io::write<float>(volume.heightFalloff)
					<< io::write<float>(volume.activeFrom)
					<< io::write<float>(volume.activeTo)
					<< io::write<float>(volume.fadeHours)
					<< io::write<float>(volume.noiseAmount)
					<< io::write<uint8>(volume.noiseDetail);
			}

			volumesChunk.Finish();
		}
	}
}
