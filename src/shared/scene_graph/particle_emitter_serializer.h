// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

namespace io
{
	class Writer;
	class Reader;
}

namespace mmo
{
	struct ParticleEmitterParameters;

	namespace particle_emitter_version
	{
		enum Type
		{
			Latest = -1,
			Version_1_0 = 0x0100
		};
	}

	typedef particle_emitter_version::Type ParticleEmitterVersion;

	/// @brief Serializer for particle emitter parameters to .hpar files
	class ParticleEmitterSerializer final : public NonCopyable
	{
	public:
		/// @brief Serializes particle emitter parameters to a binary writer
		/// @param params The parameters to serialize
		/// @param writer The binary writer to write to
		/// @param version The version to use for serialization
		void Serialize(const ParticleEmitterParameters& params, io::Writer& writer, 
			ParticleEmitterVersion version = particle_emitter_version::Latest) const;

		/// @brief Deserializes particle emitter parameters from a binary reader
		/// @param params The parameters to deserialize into
		/// @param reader The binary reader to read from
		/// @returns true if deserialization succeeded, false otherwise
		bool Deserialize(ParticleEmitterParameters& params, io::Reader& reader);
	};
}
