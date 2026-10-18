// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "client_data/project.h"
#include "math/vector3.h"

#include <memory>
#include <vector>

namespace mmo
{
	namespace nav
	{
		class Map;
	}

	/// @brief Outcome of a bot nav query.
	enum class BotNavResult
	{
		Ok,
		/// There is no nav data for the map (missing folder, unknown map or invalid nav map).
		NoNavmesh,
		/// The nav data exists but no path connects the points.
		NoPath
	};

	/// @brief Nav mesh access for the dev-only bot API, without a nav mesh shipped to players.
	/// @details Mounts the nav folder (CVars dataPath and botNavPath) into the client's existing
	///          asset registry once per process, keeps the current map's nav mesh and loads the
	///          pages a query touches on demand, synchronously. Unlike BotNavService it never
	///          initializes or destroys the asset registry, which the client owns.
	///          Main thread only: the nav mesh and its query object are not thread-safe.
	class BotNav final : public NonCopyable
	{
	public:
		explicit BotNav(const proto_client::MapManager& maps);
		~BotNav() override;

	public:
		/// @brief Finds a path on the given map, following a partial path when the goal is not
		///        reachable.
		/// @param out Receives the waypoints, including the start point.
		BotNavResult FindPath(uint32 mapId, const Vector3& from, const Vector3& to, std::vector<Vector3>& out);

		/// @brief Picks a random point on the nav mesh within radius of center.
		BotNavResult FindRandomPoint(uint32 mapId, const Vector3& center, float radius, Vector3& out);

	private:
		/// @brief Makes the map's nav mesh current. Returns false when the map has no nav data.
		bool EnsureMap(uint32 mapId);

		/// @brief Loads every page under the XZ box spanned by a and b, grown by margin.
		void EnsurePages(const Vector3& a, const Vector3& b, float margin);

	private:
		const proto_client::MapManager& m_maps;
		std::unique_ptr<nav::Map> m_map;
		uint32 m_mapId { 0 };

		/// The current map id was checked and has no usable nav data; not retried on every query.
		bool m_mapUnavailable { false };
	};
}
