// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_emitter_serializer.h"
#include "particle_emitter.h"

#include "base/chunk_writer.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	// Chunk magic numbers for .hpar file format
	static const ChunkMagic VersionChunk = MakeChunkMagic('SREV');       // 'VERS'
	static const ChunkMagic ParametersChunk = MakeChunkMagic('MRAP');    // 'PARM'
	static const ChunkMagic ColorCurveChunk = MakeChunkMagic('RLOC');    // 'COLR'

	void ParticleEmitterSerializer::Serialize(const ParticleEmitterParameters& params, 
		io::Writer& writer, ParticleEmitterVersion version) const
	{
		// Resolve version
		if (version == particle_emitter_version::Latest)
		{
			version = particle_emitter_version::Version_1_0;
		}

		// Write version chunk
		{
			ChunkWriter versionChunk(VersionChunk, writer);
			writer << io::write<uint32>(static_cast<uint32>(version));
			versionChunk.Finish();
		}

		// Write parameters chunk
		{
			ChunkWriter paramChunk(ParametersChunk, writer);
			
			// Spawn parameters
			writer << io::write<float>(params.spawnRate);
			writer << io::write<uint32>(params.maxParticles);
			
			// Shape parameters
			writer << io::write<uint8>(static_cast<uint8>(params.shape));
			writer << io::write<float>(params.shapeExtents.x);
			writer << io::write<float>(params.shapeExtents.y);
			writer << io::write<float>(params.shapeExtents.z);
			
			// Lifetime parameters
			writer << io::write<float>(params.minLifetime);
			writer << io::write<float>(params.maxLifetime);
			
			// Velocity parameters
			writer << io::write<float>(params.minVelocity.x);
			writer << io::write<float>(params.minVelocity.y);
			writer << io::write<float>(params.minVelocity.z);
			writer << io::write<float>(params.maxVelocity.x);
			writer << io::write<float>(params.maxVelocity.y);
			writer << io::write<float>(params.maxVelocity.z);
			
			// Force parameters
			writer << io::write<float>(params.gravity.x);
			writer << io::write<float>(params.gravity.y);
			writer << io::write<float>(params.gravity.z);
			
			// Size parameters
			writer << io::write<float>(params.startSize);
			writer << io::write<float>(params.endSize);
			
			// Sprite sheet parameters
			writer << io::write<uint32>(params.spriteSheetColumns);
			writer << io::write<uint32>(params.spriteSheetRows);
			writer << io::write<uint8>(params.animateSprites ? 1 : 0);
			
			// Material parameters
			writer << io::write_dynamic_range<uint8>(params.materialName);
			
			paramChunk.Finish();
		}

		// Write color curve chunk
		{
			ChunkWriter colorChunk(ColorCurveChunk, writer);
			params.colorOverLifetime.Serialize(writer);
			colorChunk.Finish();
		}
	}

	bool ParticleEmitterSerializer::Deserialize(ParticleEmitterParameters& params, io::Reader& reader)
	{
		bool hasVersion = false;
		bool hasParameters = false;
		bool hasColorCurve = false;
		ParticleEmitterVersion version = particle_emitter_version::Version_1_0;

		// Read chunks until end of stream
		while (reader)
		{
			// Read chunk header
			uint32 chunkMagic = 0;
			uint32 chunkSize = 0;
			
			reader >> io::read<uint32>(chunkMagic);
			if (!reader) break;
			
			reader >> io::read<uint32>(chunkSize);
			if (!reader) break;

			// Process chunk based on magic
			if (chunkMagic == *VersionChunk)
			{
				uint32 versionValue = 0;
				reader >> io::read<uint32>(versionValue);
				version = static_cast<ParticleEmitterVersion>(versionValue);
				
				// Validate version
				if (version != particle_emitter_version::Version_1_0)
				{
					ELOG("Unsupported particle emitter version: " << versionValue);
					return false;
				}
				
				hasVersion = true;
			}
			else if (chunkMagic == *ParametersChunk)
			{
				// Spawn parameters
				reader >> io::read<float>(params.spawnRate);
				reader >> io::read<uint32>(params.maxParticles);
				
				// Shape parameters
				uint8 shapeValue = 0;
				reader >> io::read<uint8>(shapeValue);
				params.shape = static_cast<EmitterShape>(shapeValue);
				reader >> io::read<float>(params.shapeExtents.x);
				reader >> io::read<float>(params.shapeExtents.y);
				reader >> io::read<float>(params.shapeExtents.z);
				
				// Lifetime parameters
				reader >> io::read<float>(params.minLifetime);
				reader >> io::read<float>(params.maxLifetime);
				
				// Velocity parameters
				reader >> io::read<float>(params.minVelocity.x);
				reader >> io::read<float>(params.minVelocity.y);
				reader >> io::read<float>(params.minVelocity.z);
				reader >> io::read<float>(params.maxVelocity.x);
				reader >> io::read<float>(params.maxVelocity.y);
				reader >> io::read<float>(params.maxVelocity.z);
				
				// Force parameters
				reader >> io::read<float>(params.gravity.x);
				reader >> io::read<float>(params.gravity.y);
				reader >> io::read<float>(params.gravity.z);
				
				// Size parameters
				reader >> io::read<float>(params.startSize);
				reader >> io::read<float>(params.endSize);
				
				// Sprite sheet parameters
				reader >> io::read<uint32>(params.spriteSheetColumns);
				reader >> io::read<uint32>(params.spriteSheetRows);
				uint8 animateSpritesValue = 0;
				reader >> io::read<uint8>(animateSpritesValue);
				params.animateSprites = (animateSpritesValue != 0);
				
				// Material parameters
				reader >> io::read_container<uint8>(params.materialName);
				
				hasParameters = true;
			}
			else if (chunkMagic == *ColorCurveChunk)
			{
				if (!params.colorOverLifetime.Deserialize(reader))
				{
					ELOG("Failed to deserialize color curve");
					return false;
				}
				
				hasColorCurve = true;
			}
			else
			{
				// Unknown chunk - skip it by reading chunkSize bytes
				WLOG("Unknown chunk in particle emitter file: " << chunkMagic);
				reader >> io::skip(chunkSize);
			}
		}

		// Validate that all required chunks were present
		if (!hasVersion)
		{
			ELOG("Particle emitter file missing version chunk");
			return false;
		}
		
		if (!hasParameters)
		{
			ELOG("Particle emitter file missing parameters chunk");
			return false;
		}
		
		if (!hasColorCurve)
		{
			ELOG("Particle emitter file missing color curve chunk");
			return false;
		}

		// All required chunks were read successfully
		return true;
	}
}
