// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_emitter_serializer.h"
#include "particle_emitter.h"

#include "base/chunk_writer.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	// Top-level chunk magics. 'SREV' matches the legacy version chunk so old files keep loading.
	static const ChunkMagic VersionChunk = MakeChunkMagic('SREV');       // version
	static const ChunkMagic SystemChunk = MakeChunkMagic('SYSP');        // system header (v2.0)
	static const ChunkMagic EmitterChunk = MakeChunkMagic('EMTR');       // one emitter (v2.0)
	static const ChunkMagic ParametersChunk = MakeChunkMagic('MRAP');    // legacy PARM
	static const ChunkMagic ColorCurveChunk = MakeChunkMagic('RLOC');    // legacy COLR

	namespace
	{
		// --- Inline (flat) curve serialization. Unlike ColorCurve::Serialize / FloatCurve::Serialize
		// these do NOT use the chunk reader (which would consume to end-of-stream), so the curves can
		// be embedded safely inside a larger emitter blob.

		void WriteFloatCurveInline(io::Writer& writer, const FloatCurve& curve)
		{
			const auto& keys = curve.GetKeys();
			writer << io::write<uint32>(static_cast<uint32>(keys.size()));
			for (const auto& key : keys)
			{
				writer
					<< io::write<float>(key.time)
					<< io::write<float>(key.value)
					<< io::write<float>(key.inTangent)
					<< io::write<float>(key.outTangent)
					<< io::write<uint8>(key.tangentMode);
			}
		}

		bool ReadFloatCurveInline(io::Reader& reader, FloatCurve& curve)
		{
			uint32 count = 0;
			if (!(reader >> io::read<uint32>(count)))
			{
				return false;
			}

			std::vector<FloatKey> keys;
			keys.reserve(count);
			for (uint32 i = 0; i < count; ++i)
			{
				FloatKey key;
				if (!(reader
					>> io::read<float>(key.time)
					>> io::read<float>(key.value)
					>> io::read<float>(key.inTangent)
					>> io::read<float>(key.outTangent)
					>> io::read<uint8>(key.tangentMode)))
				{
					return false;
				}
				keys.push_back(key);
			}

			curve = FloatCurve(std::move(keys));
			return true;
		}

		void WriteColorCurveInline(io::Writer& writer, const ColorCurve& curve)
		{
			const auto& keys = curve.GetKeys();
			writer << io::write<uint32>(static_cast<uint32>(keys.size()));
			for (const auto& key : keys)
			{
				writer
					<< io::write<float>(key.time)
					<< io::write<float>(key.color.x)
					<< io::write<float>(key.color.y)
					<< io::write<float>(key.color.z)
					<< io::write<float>(key.color.w)
					<< io::write<float>(key.inTangent.x)
					<< io::write<float>(key.inTangent.y)
					<< io::write<float>(key.inTangent.z)
					<< io::write<float>(key.inTangent.w)
					<< io::write<float>(key.outTangent.x)
					<< io::write<float>(key.outTangent.y)
					<< io::write<float>(key.outTangent.z)
					<< io::write<float>(key.outTangent.w)
					<< io::write<uint8>(key.tangentMode);
			}
		}

		bool ReadColorCurveInline(io::Reader& reader, ColorCurve& curve)
		{
			uint32 count = 0;
			if (!(reader >> io::read<uint32>(count)))
			{
				return false;
			}

			std::vector<ColorKey> keys;
			keys.reserve(count);
			for (uint32 i = 0; i < count; ++i)
			{
				ColorKey key;
				if (!(reader
					>> io::read<float>(key.time)
					>> io::read<float>(key.color.x)
					>> io::read<float>(key.color.y)
					>> io::read<float>(key.color.z)
					>> io::read<float>(key.color.w)
					>> io::read<float>(key.inTangent.x)
					>> io::read<float>(key.inTangent.y)
					>> io::read<float>(key.inTangent.z)
					>> io::read<float>(key.inTangent.w)
					>> io::read<float>(key.outTangent.x)
					>> io::read<float>(key.outTangent.y)
					>> io::read<float>(key.outTangent.z)
					>> io::read<float>(key.outTangent.w)
					>> io::read<uint8>(key.tangentMode)))
				{
					return false;
				}
				keys.push_back(key);
			}

			curve = ColorCurve(std::move(keys));
			return true;
		}

		void WriteEmitter(io::Writer& writer, const EmitterParameters& p)
		{
			ChunkWriter emitterChunk(EmitterChunk, writer);

			// Emitter module
			writer << io::write_dynamic_range<uint16>(p.name);
			writer << io::write<uint8>(p.enabled ? 1 : 0);
			writer << io::write<uint8>(static_cast<uint8>(p.simulationSpace));
			writer << io::write<uint8>(p.loop ? 1 : 0);
			writer << io::write<float>(p.duration);
			writer << io::write<float>(p.startDelay);
			writer << io::write<float>(p.warmupTime);
			writer << io::write<float>(p.inheritVelocity);

			// Emission module
			writer << io::write<float>(p.spawnRate);
			writer << io::write<uint32>(p.maxParticles);
			writer << io::write<uint32>(static_cast<uint32>(p.bursts.size()));
			for (const auto& burst : p.bursts)
			{
				writer << io::write<float>(burst.time);
				writer << io::write<uint32>(burst.count);
			}

			// Shape module
			writer << io::write<uint8>(static_cast<uint8>(p.shape));
			writer << io::write<float>(p.shapeExtents.x);
			writer << io::write<float>(p.shapeExtents.y);
			writer << io::write<float>(p.shapeExtents.z);

			// Spawn module
			writer << io::write<float>(p.minLifetime);
			writer << io::write<float>(p.maxLifetime);
			writer << io::write<float>(p.minVelocity.x) << io::write<float>(p.minVelocity.y) << io::write<float>(p.minVelocity.z);
			writer << io::write<float>(p.maxVelocity.x) << io::write<float>(p.maxVelocity.y) << io::write<float>(p.maxVelocity.z);
			writer << io::write<float>(p.minStartSpeed);
			writer << io::write<float>(p.maxStartSpeed);
			writer << io::write<float>(p.minStartSize);
			writer << io::write<float>(p.maxStartSize);
			writer << io::write<float>(p.minStartRotation);
			writer << io::write<float>(p.maxStartRotation);
			writer << io::write<float>(p.minAngularVelocity);
			writer << io::write<float>(p.maxAngularVelocity);

			// Update / forces module
			writer << io::write<float>(p.gravity.x) << io::write<float>(p.gravity.y) << io::write<float>(p.gravity.z);
			writer << io::write<float>(p.drag);
			writer << io::write<float>(p.orbitalSpeed);
			writer << io::write<float>(p.radialAcceleration);
			writer << io::write<float>(p.attractorPosition.x) << io::write<float>(p.attractorPosition.y) << io::write<float>(p.attractorPosition.z);
			writer << io::write<float>(p.attractorStrength);
			writer << io::write<float>(p.noiseAmplitude);
			writer << io::write<float>(p.noiseFrequency);

			// Sprite sheet
			writer << io::write<uint32>(p.spriteSheetColumns);
			writer << io::write<uint32>(p.spriteSheetRows);
			writer << io::write<uint8>(static_cast<uint8>(p.spriteAnimation));
			writer << io::write<float>(p.spriteAnimationFps);

			// Render module
			writer << io::write<uint8>(static_cast<uint8>(p.renderMode));
			writer << io::write<float>(p.lengthScale);
			writer << io::write_dynamic_range<uint16>(p.materialName);

			// Over-life curves (inline)
			WriteFloatCurveInline(writer, p.sizeOverLife);
			WriteColorCurveInline(writer, p.colorOverLifetime);

			// Mesh render module (added after v2.0). Trailing field: older readers stop at the
			// chunk boundary and simply leave meshName empty.
			writer << io::write_dynamic_range<uint16>(p.meshName);

			emitterChunk.Finish();
		}

		bool ReadEmitter(io::Reader& reader, EmitterParameters& p, std::size_t chunkEnd)
		{
			uint8 u8 = 0;

			reader >> io::read_container<uint16>(p.name);
			reader >> io::read<uint8>(u8); p.enabled = (u8 != 0);
			reader >> io::read<uint8>(u8); p.simulationSpace = static_cast<SimulationSpace>(u8);
			reader >> io::read<uint8>(u8); p.loop = (u8 != 0);
			reader >> io::read<float>(p.duration);
			reader >> io::read<float>(p.startDelay);
			reader >> io::read<float>(p.warmupTime);
			reader >> io::read<float>(p.inheritVelocity);

			reader >> io::read<float>(p.spawnRate);
			reader >> io::read<uint32>(p.maxParticles);

			uint32 burstCount = 0;
			reader >> io::read<uint32>(burstCount);
			p.bursts.clear();
			p.bursts.reserve(burstCount);
			for (uint32 i = 0; i < burstCount; ++i)
			{
				EmitterBurst burst;
				reader >> io::read<float>(burst.time);
				reader >> io::read<uint32>(burst.count);
				p.bursts.push_back(burst);
			}

			reader >> io::read<uint8>(u8); p.shape = static_cast<EmitterShape>(u8);
			reader >> io::read<float>(p.shapeExtents.x);
			reader >> io::read<float>(p.shapeExtents.y);
			reader >> io::read<float>(p.shapeExtents.z);

			reader >> io::read<float>(p.minLifetime);
			reader >> io::read<float>(p.maxLifetime);
			reader >> io::read<float>(p.minVelocity.x) >> io::read<float>(p.minVelocity.y) >> io::read<float>(p.minVelocity.z);
			reader >> io::read<float>(p.maxVelocity.x) >> io::read<float>(p.maxVelocity.y) >> io::read<float>(p.maxVelocity.z);
			reader >> io::read<float>(p.minStartSpeed);
			reader >> io::read<float>(p.maxStartSpeed);
			reader >> io::read<float>(p.minStartSize);
			reader >> io::read<float>(p.maxStartSize);
			reader >> io::read<float>(p.minStartRotation);
			reader >> io::read<float>(p.maxStartRotation);
			reader >> io::read<float>(p.minAngularVelocity);
			reader >> io::read<float>(p.maxAngularVelocity);

			reader >> io::read<float>(p.gravity.x) >> io::read<float>(p.gravity.y) >> io::read<float>(p.gravity.z);
			reader >> io::read<float>(p.drag);
			reader >> io::read<float>(p.orbitalSpeed);
			reader >> io::read<float>(p.radialAcceleration);
			reader >> io::read<float>(p.attractorPosition.x) >> io::read<float>(p.attractorPosition.y) >> io::read<float>(p.attractorPosition.z);
			reader >> io::read<float>(p.attractorStrength);
			reader >> io::read<float>(p.noiseAmplitude);
			reader >> io::read<float>(p.noiseFrequency);

			reader >> io::read<uint32>(p.spriteSheetColumns);
			reader >> io::read<uint32>(p.spriteSheetRows);
			reader >> io::read<uint8>(u8); p.spriteAnimation = static_cast<SpriteAnimationMode>(u8);
			reader >> io::read<float>(p.spriteAnimationFps);

			reader >> io::read<uint8>(u8); p.renderMode = static_cast<ParticleRenderMode>(u8);
			reader >> io::read<float>(p.lengthScale);
			reader >> io::read_container<uint16>(p.materialName);

			if (!ReadFloatCurveInline(reader, p.sizeOverLife))
			{
				return false;
			}
			if (!ReadColorCurveInline(reader, p.colorOverLifetime))
			{
				return false;
			}

			// Optional trailing fields appended after v2.0. Only read them when the emitter chunk
			// actually contains more bytes, so legacy v2.0 files (which end at the colour curve)
			// keep loading without consuming bytes from the following chunk.
			if (reader && reader.getSource()->position() < chunkEnd)
			{
				reader >> io::read_container<uint16>(p.meshName);
			}

			return static_cast<bool>(reader);
		}

		/// @brief Reads the legacy v1.0 PARM chunk into a single emitter, mapping start/end size to a curve.
		bool ReadLegacyParameters(io::Reader& reader, EmitterParameters& p)
		{
			float startSize = 1.0f;
			float endSize = 0.0f;

			reader >> io::read<float>(p.spawnRate);
			reader >> io::read<uint32>(p.maxParticles);

			uint8 shapeValue = 0;
			reader >> io::read<uint8>(shapeValue);
			p.shape = static_cast<EmitterShape>(shapeValue);
			reader >> io::read<float>(p.shapeExtents.x);
			reader >> io::read<float>(p.shapeExtents.y);
			reader >> io::read<float>(p.shapeExtents.z);

			reader >> io::read<float>(p.minLifetime);
			reader >> io::read<float>(p.maxLifetime);

			reader >> io::read<float>(p.minVelocity.x) >> io::read<float>(p.minVelocity.y) >> io::read<float>(p.minVelocity.z);
			reader >> io::read<float>(p.maxVelocity.x) >> io::read<float>(p.maxVelocity.y) >> io::read<float>(p.maxVelocity.z);

			reader >> io::read<float>(p.gravity.x) >> io::read<float>(p.gravity.y) >> io::read<float>(p.gravity.z);

			reader >> io::read<float>(startSize);
			reader >> io::read<float>(endSize);

			reader >> io::read<uint32>(p.spriteSheetColumns);
			reader >> io::read<uint32>(p.spriteSheetRows);

			uint8 animateSprites = 0;
			reader >> io::read<uint8>(animateSprites);
			p.spriteAnimation = (animateSprites != 0) ? SpriteAnimationMode::AnimateOverLife : SpriteAnimationMode::None;

			reader >> io::read_container<uint8>(p.materialName);

			// Map legacy start/end absolute size into baseSize + a normalized size-over-life curve.
			const float ref = (startSize != 0.0f) ? startSize : (endSize != 0.0f ? endSize : 1.0f);
			p.minStartSize = ref;
			p.maxStartSize = ref;
			p.sizeOverLife = FloatCurve(startSize / ref, endSize / ref);

			return static_cast<bool>(reader);
		}
	}

	// ========================================================================
	// ParticleSystemSerializer
	// ========================================================================

	void ParticleSystemSerializer::Serialize(const ParticleSystemParameters& params, io::Writer& writer,
		ParticleEmitterVersion version) const
	{
		if (version == particle_emitter_version::Latest)
		{
			version = particle_emitter_version::Version_2_0;
		}

		// Version chunk
		{
			ChunkWriter versionChunk(VersionChunk, writer);
			writer << io::write<uint32>(static_cast<uint32>(version));
			versionChunk.Finish();
		}

		// System chunk
		{
			ChunkWriter systemChunk(SystemChunk, writer);
			writer << io::write<uint32>(static_cast<uint32>(params.emitters.size()));
			systemChunk.Finish();
		}

		// Emitter chunks
		for (const auto& emitter : params.emitters)
		{
			WriteEmitter(writer, emitter);
		}
	}

	bool ParticleSystemSerializer::Deserialize(ParticleSystemParameters& params, io::Reader& reader) const
	{
		params.emitters.clear();

		bool hasVersion = false;
		ParticleEmitterVersion version = particle_emitter_version::Version_1_0;

		// For the legacy path we accumulate into a single emitter.
		EmitterParameters legacyEmitter;
		bool legacyHasParams = false;
		bool legacyHasColor = false;

		while (reader)
		{
			uint32 chunkMagic = 0;
			uint32 chunkSize = 0;

			if (!(reader >> io::read<uint32>(chunkMagic)))
			{
				break;
			}
			if (!(reader >> io::read<uint32>(chunkSize)))
			{
				break;
			}

			const auto chunkEnd = reader.getSource()->position() + chunkSize;

			if (chunkMagic == *VersionChunk)
			{
				uint32 versionValue = 0;
				reader >> io::read<uint32>(versionValue);
				version = static_cast<ParticleEmitterVersion>(versionValue);
				hasVersion = true;
			}
			else if (chunkMagic == *SystemChunk)
			{
				uint32 emitterCount = 0;
				reader >> io::read<uint32>(emitterCount);
				// Count is informational; emitters are read from the EMIT chunks that follow.
			}
			else if (chunkMagic == *EmitterChunk)
			{
				EmitterParameters emitter;
				if (!ReadEmitter(reader, emitter, chunkEnd))
				{
					ELOG("Failed to read emitter chunk in particle system");
					return false;
				}
				params.emitters.push_back(std::move(emitter));
				// Defensive: realign to the declared chunk end.
				reader.getSource()->seek(chunkEnd);
			}
			else if (chunkMagic == *ParametersChunk)
			{
				if (!ReadLegacyParameters(reader, legacyEmitter))
				{
					ELOG("Failed to read legacy particle parameters chunk");
					return false;
				}
				legacyHasParams = true;
				// Defensive: realign to the declared chunk end.
				reader.getSource()->seek(chunkEnd);
			}
			else if (chunkMagic == *ColorCurveChunk)
			{
				// Legacy COLR is the final chunk; its chunk-based deserializer consumes to EOF.
				if (!legacyEmitter.colorOverLifetime.Deserialize(reader))
				{
					ELOG("Failed to deserialize legacy color curve");
					return false;
				}
				legacyHasColor = true;
			}
			else
			{
				WLOG("Unknown chunk in particle system file: " << chunkMagic);
				reader.getSource()->seek(chunkEnd);
			}
		}

		if (!hasVersion)
		{
			ELOG("Particle system file missing version chunk");
			return false;
		}

		// Legacy single-emitter file: finalize the one emitter.
		if (version == particle_emitter_version::Version_1_0)
		{
			if (!legacyHasParams)
			{
				ELOG("Legacy particle file missing parameters chunk");
				return false;
			}
			(void)legacyHasColor;
			params.emitters.clear();
			params.emitters.push_back(std::move(legacyEmitter));
		}

		if (params.emitters.empty())
		{
			ELOG("Particle system file contained no emitters");
			return false;
		}

		return true;
	}

	// ========================================================================
	// ParticleEmitterSerializer (backward-compatible single emitter)
	// ========================================================================

	void ParticleEmitterSerializer::Serialize(const EmitterParameters& params, io::Writer& writer,
		ParticleEmitterVersion version) const
	{
		ParticleSystemParameters systemParams;
		systemParams.emitters.push_back(params);

		ParticleSystemSerializer serializer;
		serializer.Serialize(systemParams, writer, version);
	}

	bool ParticleEmitterSerializer::Deserialize(EmitterParameters& params, io::Reader& reader) const
	{
		ParticleSystemParameters systemParams;
		ParticleSystemSerializer serializer;
		if (!serializer.Deserialize(systemParams, reader))
		{
			return false;
		}

		if (systemParams.emitters.empty())
		{
			return false;
		}

		params = systemParams.emitters.front();
		return true;
	}
}
