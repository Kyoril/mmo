// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/chunk_reader.h"
#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/quaternion.h"

#include <vector>

namespace io
{
	class Writer;
}

namespace mmo
{
	/// @brief A single authored, instanced-rendered foliage placement (e.g. a tree).
	/// @details Unlike procedural grass, every instance is explicitly placed, has a stable
	///          unique id (used for editing, selection and collision identity) and is
	///          persisted. Instances are grouped into per-page .hfol files and rendered
	///          through hardware-instanced render chunks.
	struct FoliageInstance
	{
		/// @brief Stable unique identifier for this instance.
		uint64 uniqueId = 0;

		/// @brief The mesh asset used for this instance.
		String meshName;

		/// @brief World-space position.
		Vector3 position;

		/// @brief World-space orientation.
		Quaternion rotation;

		/// @brief World-space scale.
		Vector3 scale = Vector3::UnitScale;
	};

	namespace foliage_version
	{
		enum Type
		{
			Latest = -1,

			Version_0_0_0_1 = 0x0001,
		};
	}

	typedef foliage_version::Type FoliageVersion;

	/// @brief Loads the foliage instances of a single terrain page from a .hfol file.
	/// @details The file is a chunked binary format:
	///          FVER (version), FMSH (deduped mesh-name table), FINS (instance records).
	///          Has no graphics dependency so it can be consumed by the client renderer,
	///          the world editor, the server collision map and the navmesh builder alike.
	class WorldFoliageLoader final : public ChunkReader
	{
	public:
		explicit WorldFoliageLoader();

		/// @brief Gets the instances that were read from the file.
		[[nodiscard]] const std::vector<FoliageInstance>& GetInstances() const
		{
			return m_instances;
		}

		/// @brief Takes ownership of the loaded instances, leaving the loader empty.
		[[nodiscard]] std::vector<FoliageInstance>&& TakeInstances()
		{
			return std::move(m_instances);
		}

	private:
		bool OnVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool OnMeshNamesChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool OnInstanceChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		uint32 m_version = 0;
		std::vector<String> m_meshNames;
		std::vector<FoliageInstance> m_instances;
	};

	/// @brief Serializes a set of foliage instances into a .hfol page file.
	class WorldFoliageSerializer final
	{
	public:
		/// @brief Writes the given instances to the writer in the .hfol chunked format.
		/// @param writer The destination writer.
		/// @param instances The instances to serialize (may be empty).
		static void Write(io::Writer& writer, const std::vector<FoliageInstance>& instances);
	};
}
