
#pragma once

#include <fstream>

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/filesystem.h"

#include "map.h"

#include <map>
#include <mutex>
#include <vector>

#include "stream_sink.h"

namespace mmo
{
	/// This struct represents a tile index.
	struct TileIndex
	{
		/// The x coordinate of the tile.
		int32 x;
		/// The y coordinate of the tile.
		int32 y;
	};

	/// This class contains serializable data which can be used to construct a page of the navigation map used to load
	///	by the server application.
	class SerializableNavPage final : public NonCopyable
	{
	protected:
		// Serialized heightfield and finalized mesh data, mapped by global tile id
		std::map<std::pair<int32, int32>, std::vector<char>> m_tiles;

		// Serialized data for page quad heights, mapped by global tile id within the page
		std::map<std::pair<int32, int32>, std::vector<char>> m_quadHeights;

		/// Mutex used to lock adding tiles to the page, as this is done from multiple threads.
		mutable std::mutex m_mutex;

	public:
		/// Adds a tile to the page. If there was already tile data for the given tile, it is replaced.
		///	@param x The global x coordinate of the tile.
		///	@param y The global y coordinate of the tile.
		///	@param quadHeights The serialized quad height data for the tile.
		///	@param heightField The serialized height field and finalized tile buffer for the tile.
		void AddTile(int32 x, int32 y, std::vector<char>&& quadHeights, std::vector<char>&& heightField);

		/// Determines whether all page tiles have been added.
		///	@returns True if all tiles have been added, false otherwise.
		bool IsComplete() const
		{
			return m_tiles.size() == terrain::constants::TilesPerPage * terrain::constants::TilesPerPage;
		}

	private:
		/// Global x coordinate of the page.
		int32 m_x;
		/// Global y coordinate of the page.
		int32 m_y;

	public:
		/// Creates a new instance of the SerializableNavPage class and initializes it.
		///	@param x The global x coordinate of the page.
		///	@param y The global y coordinate of the page.
		SerializableNavPage(const int32 x, const int32 y)
			: m_x(x)
			, m_y(y)
		{
		}

	public:
		/// Serializes the page data using the specified io Writer.
		///	@param writer The io Writer to use for serialization. This abstracts the writing process.
		void Serialize(io::Writer& writer) const
		{
			// Write header
			writer
				<< io::write<uint32>(0)
				<< io::write<uint32>(0)
				<< io::write<uint32>(0);

			// Write global coordinates
			writer
				<< static_cast<uint32>(m_x)
				<< static_cast<uint32>(m_y);

			// Write tile count
			writer << io::write<uint32>(m_tiles.size());

			// Write data of all tiles
			for (auto const& tile : m_tiles)
			{
				// we want to store the global tile x and y, rather than the x, y relative to this page
				auto const x = static_cast<std::uint32_t>(tile.first.first) + m_x * terrain::constants::TilesPerPage;
				auto const y = static_cast<std::uint32_t>(tile.first.second) + m_y * terrain::constants::TilesPerPage;

				writer << io::write<uint32>(x) << io::write<uint32>(y);

				// append terrain quad height data
				writer << io::write_range(m_quadHeights.at(tile.first));

				// height field and finalized tile buffer
				writer << io::write_range(tile.second);
			}

			// Ensure the writer is flushed since we're done writing
			writer.Sink().Flush();
		}
	};

	/// This class is the heart of the navigation mesh building process. It is responsible for building navigation maps
	///	out of the actual game world data.
	class MeshBuilder final : public NonCopyable
	{
	public:
		/// Creates a new instance of the MeshBuilder class and initializes it.
		///	@param outputPath The path to the output directory where the navigation map files will be stored.
		///	@param worldName The name of the world for which the navigation map is being built.
		explicit MeshBuilder(String outputPath, String worldName);

	public:
		/// Gets the next tile index to be processed, thread safe. If no tiles are left to process, the function returns false which
		///	indicates that the builder is actually finished.
		///	@param tile Output ref. The next tile index.
		///	@returns True if a tile was found, false otherwise.
		[[nodiscard]] bool GetNextTile(TileIndex& tile);

		/// Gets the number of completed tiles. Can be used to determine if the builder is finished.
		///	@returns The number of completed tiles.
		[[nodiscard]] size_t CompletedTiles() const { return m_completedTiles; }

		/// Gets the number of available tiles to be processed.
		///	@returns The number of available tiles.
		[[nodiscard]] size_t GetTileCount() const { return m_totalTiles; }

		/// Gets the completion percentage of the builder as actual percent float value (0 - 100 range).
		///	@returns The completion percentage in a range of 0..100.
		[[nodiscard]] float PercentComplete() const;

		/// Builds and serializes the navigation map for the specified tile. Thread safe.
		///	@param tile The tile index to build and serialize. Should be obtained by GetNextTile.
		///	@returns True if the tile was successfully built and serialized, false otherwise.
		[[nodiscard]] bool BuildAndSerializeTerrainTile(const TileIndex& tile);

	private:

		/// Increments the reference counter of the given chunk. This is used to determine if pages can be unloaded or not
		///	once a tile has been completely built.
		///	@param chunkX The global x coordinate of the chunk.
		///	@param chunkY The global y coordinate of the chunk.
		void AddChunkReference(int32 chunkX, int32 chunkY);

		/// Decrements the reference counter of a given chunk. If all chunk ref counts of a page are zero, the page will
		///	actually be unloaded.
		///	@param chunkX The global x coordinate of the chunk.
		///	@param chunkY The global y coordinate of the chunk.
		void RemoveChunkReference(int32 chunkX, int32 chunkY);

		/// Gets a serializable page that at the given global page coordinate. If not loaded, a new instance is created.
		/// This can be used to store tile page data in memory because we actually build tilesasync , not pages.
		/// Assumes ownership of m_mutex.
		///	@param x The global x coordinate of the page.
		///	@param y The global y coordinate of the page.
		///	@returns Pointer to the serializable page.
		SerializableNavPage* GetInProgressPage(int32 x, int32 y);

		/// Unloads a loaded page from memory entirely, also freeing memory. This should only be done if this page data
		///	is no longer needed by any neighbour page. Assumes ownership of m_mutex.
		void RemovePage(const SerializableNavPage* page);

	private:
		std::unique_ptr<Map> m_map;

		String m_outputPath;
		String m_worldPath;

		size_t m_totalTiles = 0;
		volatile size_t m_completedTiles = 0;

		std::vector<TileIndex> m_pendingTiles;
		std::vector<int32> m_chunkReferences;
		mutable std::mutex m_mutex;

		std::map<std::pair<int32, int32>, std::unique_ptr<SerializableNavPage>> m_pagesInProgress;
	};
}
