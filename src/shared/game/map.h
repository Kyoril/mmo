#pragma once

#include <filesystem>

#include "base/chunk_reader.h"

namespace mmo
{
	/// @brief Map class which represents a level in the game on the client.
	class Map : public ChunkReader
	{
	public:
		Map();

	public:
		void Load(const std::filesystem::path& filename);

	private:
		uint32 m_version;
	};
}
