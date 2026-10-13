// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

namespace mmo
{
	namespace nav
	{
		class Map;
		class PageStreamer;
	}

	class ServerCollisionMap;
	class ServerWaterMap;

	/// @brief The read-only data of one map, loaded once and shared by every world instance of it:
	/// nav mesh (streamed in), static collision geometry and water.
	/// @details Per-instance state (door collision, the nav query filter) lives in NavMapData.
	/// World thread only: Detour is not thread safe and the world server runs single threaded.
	class MapStaticData final : public NonCopyable
	{
	public:
		/// @brief Loads the map's collision and water data and starts streaming its nav mesh.
		/// @param directory The map directory (proto::MapEntry::directory()).
		explicit MapStaticData(const std::string& directory);
		~MapStaticData() override;

	public:
		/// @brief The map directory this data was loaded from.
		[[nodiscard]] const std::string& GetDirectory() const { return m_directory; }

		/// @brief The shared nav mesh. Pages may still be streaming in, see EnsureNavLoaded.
		[[nodiscard]] nav::Map& GetNavMap() { return *m_navMap; }

		/// @copydoc GetNavMap
		[[nodiscard]] const nav::Map& GetNavMap() const { return *m_navMap; }

		/// @brief Installs every nav page overlapping the XZ rectangle. No-op once streaming is complete.
		void EnsureNavLoaded(float minX, float minZ, float maxX, float maxZ);

		/// @brief Whether nav pages are still streaming in.
		[[nodiscard]] bool IsNavStreaming() const { return m_pageStreamer != nullptr; }

		/// @brief Installs streamed nav pages within a time budget. Called once per world tick.
		void Update();

		/// @brief Static collision geometry, for per-instance ServerCollisionMap views.
		[[nodiscard]] std::shared_ptr<const ServerCollisionMap> GetStaticCollision() const { return m_staticCollision; }

		/// @brief Water data, or null when the map has no water.
		[[nodiscard]] const ServerWaterMap* GetWaterMap() const { return m_waterMap.get(); }

	private:
		const std::string m_directory;
		std::unique_ptr<nav::Map> m_navMap;
		/// Declared after m_navMap, which it fills, so it is destroyed first. Reset once complete.
		std::unique_ptr<nav::PageStreamer> m_pageStreamer;
		std::chrono::steady_clock::time_point m_navStreamStart;
		std::shared_ptr<const ServerCollisionMap> m_staticCollision;
		std::unique_ptr<ServerWaterMap> m_waterMap;
	};

	/// @brief Hands out one MapStaticData per map directory, shared by all instances of that map.
	/// @details Holds only weak references: the data is freed when the last instance using it is
	/// destroyed, and loaded again by the next Acquire. World thread only.
	class MapStaticDataRegistry final : public NonCopyable
	{
	public:
		/// @brief Returns the live data of a map, loading it if no instance holds it.
		[[nodiscard]] std::shared_ptr<MapStaticData> Acquire(const std::string& directory);

		/// @brief Updates every live map's data once (nav page streaming) and forgets freed maps.
		void Update();

		/// @brief Number of maps whose data is currently alive.
		[[nodiscard]] size_t GetLiveCount();

	private:
		void Prune();

	private:
		std::unordered_map<std::string, std::weak_ptr<MapStaticData>> m_maps;
	};
}
