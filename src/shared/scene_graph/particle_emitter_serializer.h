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
	struct EmitterParameters;
	struct ParticleSystemParameters;

	namespace particle_emitter_version
	{
		enum Type
		{
			Latest = -1,
			Version_1_0 = 0x0100,  ///< Legacy single-emitter format
			Version_2_0 = 0x0200   ///< Multi-emitter system format
		};
	}

	typedef particle_emitter_version::Type ParticleEmitterVersion;

	/// @brief Serializes a whole particle system (multiple emitters) to/from .hpar files.
	///
	/// The v2.0 format stores a SYS chunk followed by one EMIT chunk per emitter. Legacy v1.0
	/// files (a single emitter) are detected automatically and loaded into a one-emitter system.
	class ParticleSystemSerializer final : public NonCopyable
	{
	public:
		void Serialize(const ParticleSystemParameters& params, io::Writer& writer,
			ParticleEmitterVersion version = particle_emitter_version::Latest) const;

		bool Deserialize(ParticleSystemParameters& params, io::Reader& reader) const;
	};

	/// @brief Backward-compatible single-emitter serializer.
	///
	/// Reads/writes the first emitter of a system. Writing always produces the v2.0 format with a
	/// single emitter. Provided so existing call sites keep compiling unchanged.
	class ParticleEmitterSerializer final : public NonCopyable
	{
	public:
		void Serialize(const EmitterParameters& params, io::Writer& writer,
			ParticleEmitterVersion version = particle_emitter_version::Latest) const;

		bool Deserialize(EmitterParameters& params, io::Reader& reader) const;
	};
}
