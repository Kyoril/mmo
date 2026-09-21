// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "fog_volume.h"

#include "base/chunk_reader.h"
#include "base/typedefs.h"

#include <vector>

namespace io
{
	class Writer;
}

namespace mmo
{
	namespace fog_volume_version
	{
		enum Type
		{
			Latest = -1,

			Version_0_0_0_1 = 0x0001,
		};
	}

	typedef fog_volume_version::Type FogVolumeVersion;

	/// @brief Reads the fog volumes of a single map from its `.hfog` file.
	/// @details The file is a chunked binary format: FGVR (version), FGVL (volume records).
	///          Has no graphics dependency so it can be consumed by the client renderer and the
	///          world editor alike. Every volume that is read is passed through
	///          SanitizeFogVolume() so a hand-edited or stale file can never yield out-of-range
	///          values.
	class WorldFogVolumeDeserializer final : public ChunkReader
	{
	public:
		/// @brief Creates the deserializer, appending every fog volume it reads to the given vector.
		/// @param out Destination the read volumes are appended to.
		explicit WorldFogVolumeDeserializer(std::vector<FogVolume>& out);

	private:
		bool OnVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool OnVolumesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		std::vector<FogVolume>& m_volumes;
		uint32 m_version = 0;
	};

	/// @brief Serializes a set of fog volumes into a `.hfog` file.
	class WorldFogVolumeSerializer final
	{
	public:
		/// @brief Writes the given volumes to the writer in the .hfog chunked format.
		/// @param writer The destination writer.
		/// @param volumes The volumes to serialize (may be empty).
		static void Write(io::Writer& writer, const std::vector<FogVolume>& volumes);
	};
}
