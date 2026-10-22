// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_nav_service.h"

#include "assets/asset_registry.h"
#include "base/macros.h"
#include "log/default_log_levels.h"
#include "nav_mesh/instance_query_filter.h"
#include "nav_mesh/map.h"

#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

#include <cmath>
#include <limits>
#include <sstream>

namespace fs = std::filesystem;

namespace mmo
{
	namespace
	{
		/// How far above or below the requested target a path may end and still count as
		/// reaching it. Steps and slopes stay well under this; a storey does not.
		constexpr float MaxPathEndHeightGap = 3.0f;

		std::string ToLogPath(const fs::path& path)
		{
			return path.lexically_normal().generic_string();
		}

		std::string BuildMapDetails(const uint32 mapId, std::string_view mapDirectory = {})
		{
			std::ostringstream details;
			details << "map_id=" << mapId;
			if (!mapDirectory.empty())
			{
				details << " map_directory=" << mapDirectory;
			}
			return details.str();
		}
	}

	namespace
	{
		/// Number of live navigation services in this process. AssetRegistry is a class of
		/// statics, so a second service would initialize it a second time and the first
		/// destruction would pull it out from under everyone still using it. A swarm therefore
		/// shares one service across all of its sessions; this counter makes that structural
		/// rather than a convention.
		int32 g_navServiceInstanceCount = 0;
	}

	BotNavService::BotNavService(fs::path repoRoot)
		: m_repoRoot(std::move(repoRoot))
	{
		// ASSERT compiles away in Release, which is what a swarm runs, so a second instance has to
		// be survivable rather than merely forbidden. It refuses to touch the shared registry: this
		// one ends up with no navigation data and fails every path query, which is loud and local,
		// where initializing twice would corrupt the registry underneath the instance that works.
		ASSERT(g_navServiceInstanceCount == 0 && "Only one BotNavService may exist per process");

		if (g_navServiceInstanceCount > 0)
		{
			ELOG("A BotNavService already exists in this process. AssetRegistry is a class of statics,"
				" so this one will not initialize it and will not resolve any path. Share the first"
				" instance instead - see src/shared/bot_core/README.md.");
			++g_navServiceInstanceCount;
			return;
		}

		++g_navServiceInstanceCount;

		static_cast<void>(Initialize());
	}

	BotNavService::~BotNavService()
	{
		m_loadedMaps.clear();
		if (m_assetRegistryInitialized)
		{
			AssetRegistry::Destroy();
		}

		--g_navServiceInstanceCount;
	}

	bool BotNavService::Initialize()
	{
		if (m_repoRoot.empty())
		{
			const std::optional<fs::path> resolvedRepoRoot = ResolveRepoRoot(fs::current_path());
			if (!resolvedRepoRoot.has_value())
			{
				m_failureReason = "project_load_failed";
				LogDecisionOnce("abort", m_failureReason, "repo_root=unresolved current_path=" + ToLogPath(fs::current_path()), true);
				return false;
			}

			m_repoRoot = *resolvedRepoRoot;
		}
		else
		{
			m_repoRoot = fs::absolute(m_repoRoot);
		}

		m_projectDataPath = m_repoRoot / "data" / "editor" / "data";
		m_navDataPath = m_repoRoot / "data" / "editor" / "nav";
		const fs::path projectFilePath = m_projectDataPath / "project.txt";

		if (!fs::exists(projectFilePath) || !fs::exists(m_navDataPath))
		{
			m_failureReason = "project_load_failed";
			LogDecisionOnce(
				"abort",
				m_failureReason,
				"repo_root=" + ToLogPath(m_repoRoot) +
				" project_file=" + ToLogPath(projectFilePath) +
				" nav_root=" + ToLogPath(m_navDataPath),
				true);
			return false;
		}

		AssetRegistry::Initialize(m_navDataPath, {});
		m_assetRegistryInitialized = true;

		if (!m_project.load(m_projectDataPath.string()))
		{
			m_failureReason = "project_load_failed";
			LogDecisionOnce(
				"abort",
				m_failureReason,
				"repo_root=" + ToLogPath(m_repoRoot) + " project_data=" + ToLogPath(m_projectDataPath),
				true);
			return false;
		}

		m_ready = true;
		ClearDecisionDedup();
		ILOG("bot_nav state=ready repo_root=" << ToLogPath(m_repoRoot)
			<< " project_data=" << ToLogPath(m_projectDataPath)
			<< " nav_data=" << ToLogPath(m_navDataPath)
			<< " map_count=" << m_project.maps.count());
		return true;
	}

	std::optional<fs::path> BotNavService::ResolveRepoRoot(fs::path start)
	{
		if (start.empty())
		{
			return std::nullopt;
		}

		start = fs::absolute(start);
		if (fs::is_regular_file(start))
		{
			start = start.parent_path();
		}

		for (fs::path candidate = start; !candidate.empty(); candidate = candidate.parent_path())
		{
			if (fs::exists(candidate / "data" / "editor" / "data" / "project.txt")
				&& fs::exists(candidate / "data" / "editor" / "nav"))
			{
				return candidate;
			}

			const fs::path parent = candidate.parent_path();
			if (parent == candidate)
			{
				break;
			}
		}

		return std::nullopt;
	}

	std::optional<std::string> BotNavService::ResolveMapDirectory(const uint32 mapId) const
	{
		if (!m_ready)
		{
			return std::nullopt;
		}

		const proto::MapEntry* mapEntry = m_project.maps.getById(mapId);
		if (!mapEntry || mapEntry->directory().empty())
		{
			return std::nullopt;
		}

		return mapEntry->directory();
	}

	bool BotNavService::EnsureMapAvailable(const uint32 mapId)
	{
		if (!m_ready)
		{
			static_cast<void>(FailPath(mapId, {}, "project_load_failed", "service_state=uninitialized", true));
			return false;
		}

		std::string mapDirectory;
		std::string reason;
		const std::shared_ptr<nav::Map> map = GetOrLoadMap(mapId, mapDirectory, reason);
		if (!map)
		{
			static_cast<void>(FailPath(mapId, std::move(mapDirectory), std::move(reason), "phase=ensure_map", false));
			return false;
		}

		ClearDecisionDedup();
		return true;
	}

	namespace
	{
		/// How close a door poly's centre must be to a closed door for the door to stand on it. A
		/// doorway strip is a few yards wide; the next door is further than this.
		constexpr float ClosedDoorPolyRadius = 5.0f;

		/// The bot's own path filter: the default one, minus the door polys of the closed doors it
		/// sees. The server blocks those per instance with the door's exact footprint; the bot
		/// knows only where the doors stand, which tells one doorway from the next.
		class ClosedDoorFilter final : public dtQueryFilter
		{
		public:
			explicit ClosedDoorFilter(const std::vector<Vector3>& closedDoors)
				: m_closedDoors(closedDoors)
			{
			}

			bool passFilter(const dtPolyRef ref, const dtMeshTile* tile, const dtPoly* poly) const override
			{
				if (!dtQueryFilter::passFilter(ref, tile, poly))
				{
					return false;
				}

				if ((poly->flags & nav::DoorPolyFlag) == 0 || poly->vertCount == 0)
				{
					return true;
				}

				Vector3 centre = Vector3::Zero;
				for (unsigned int i = 0; i < poly->vertCount; ++i)
				{
					const float* vertex = &tile->verts[poly->verts[i] * 3];
					centre += Vector3(vertex[0], vertex[1], vertex[2]);
				}
				centre /= static_cast<float>(poly->vertCount);

				for (const Vector3& door : m_closedDoors)
				{
					const float dx = door.x - centre.x;
					const float dz = door.z - centre.z;
					if (dx * dx + dz * dz <= ClosedDoorPolyRadius * ClosedDoorPolyRadius)
					{
						return false;
					}
				}

				return true;
			}

		private:
			const std::vector<Vector3>& m_closedDoors;
		};
	}

	BotPathResult BotNavService::FindPath(const uint32 mapId, const Vector3& start, const Vector3& end, const std::vector<Vector3>& closedDoors)
	{
		if (!m_ready)
		{
			return FailPath(mapId, {}, "project_load_failed", "service_state=uninitialized", true);
		}

		std::string mapDirectory;
		std::string reason;
		const std::shared_ptr<nav::Map> map = GetOrLoadMap(mapId, mapDirectory, reason);
		if (!map)
		{
			return FailPath(mapId, std::move(mapDirectory), std::move(reason), "phase=find_path", false);
		}

		BotPathResult result;
		result.mapId = mapId;
		result.mapDirectory = mapDirectory;

		const ClosedDoorFilter doorFilter(closedDoors);
		const dtQueryFilter* filter = closedDoors.empty() ? nullptr : &doorFilter;
		if (!map->FindPath(start, end, result.points, true, filter))
		{
			// The start is not on the mesh where the bot stands. Nearest first: a step or two to the
			// side - beside the mesh, pushed against a wall, at a gap in a floor - then a little
			// above, and at most two yards down, which with the query's own reach still finds the
			// ground under a bot hovering a few yards over a slope after a relog.
			// Never further down: from a gap in the Oakenshire town hall's floor a probe four yards
			// down found the terrain under the building, the bot walked down there through the
			// floor, and from under it never found the floor again. Two yards down from a floor, the
			// floor is still the nearest polygon.
			bool found = false;
			const Vector3 probes[] = {
				{ 2.0f, 0.0f, 0.0f }, { -2.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 2.0f }, { 0.0f, 0.0f, -2.0f },
				{ 1.5f, 0.0f, 1.5f }, { -1.5f, 0.0f, 1.5f }, { 1.5f, 0.0f, -1.5f }, { -1.5f, 0.0f, -1.5f },
				{ 0.0f, 2.0f, 0.0f }, { 0.0f, 4.0f, 0.0f },
				{ 4.0f, 0.0f, 0.0f }, { -4.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 4.0f }, { 0.0f, 0.0f, -4.0f },
				{ 0.0f, -2.0f, 0.0f },
			};
			for (const Vector3& offset : probes)
			{
				if (map->FindPath(start + offset, end, result.points, true, filter))
				{
					found = true;
					break;
				}
			}

			if (!found)
			{
				return FailPath(mapId, std::move(mapDirectory), "invalid_path", "phase=find_path status=query_failed", false);
			}
		}

		if (result.points.size() < 2)
		{
			return FailPath(mapId, std::move(mapDirectory), "invalid_path", "phase=find_path status=malformed_path point_count=" + std::to_string(result.points.size()), false);
		}

		// A partial path ends at the reachable point closest to the target, and for a target on
		// another level - a rat in a closed-off cellar - that is the spot on the floor right above
		// it. Following it put bots on top of what they meant to fight, and in plan they were
		// standing on it. Falling short on the same level is still worth walking; arriving on the
		// wrong floor is not a path to the target at all.
		const float endHeightGap = std::abs(result.points.back().y - end.y);
		if (endHeightGap > MaxPathEndHeightGap)
		{
			return FailPath(mapId, std::move(mapDirectory), "other_level",
				"phase=find_path status=ends_on_other_level height_gap=" + std::to_string(endHeightGap), false);
		}

		result.success = true;
		ClearDecisionDedup();
		return result;
	}

	std::optional<uint32> BotNavService::InferMapId(const Vector3& start, const Vector3& end)
	{
		if (!m_ready)
		{
			return std::nullopt;
		}

		const auto& maps = m_project.maps.getTemplates();
		std::optional<uint32> resolvedMapId;
		size_t candidateCount = 0;
		std::string resolvedDirectory;
		size_t resolvedPointCount = std::numeric_limits<size_t>::max();

		for (int mapIndex = 0; mapIndex < maps.entry_size(); ++mapIndex)
		{
			const auto& mapEntry = maps.entry(mapIndex);
			if (mapEntry.directory().empty())
			{
				continue;
			}

			std::string mapDirectory;
			std::string reason;
			const std::shared_ptr<nav::Map> map = GetOrLoadMap(mapEntry.id(), mapDirectory, reason);
			if (!map)
			{
				continue;
			}

			std::vector<Vector3> points;
			if (!map->FindPath(start, end, points, true) || points.size() < 2)
			{
				continue;
			}

			++candidateCount;
			if (!resolvedMapId.has_value() || points.size() < resolvedPointCount)
			{
				resolvedMapId = mapEntry.id();
				resolvedDirectory = mapDirectory;
				resolvedPointCount = points.size();
			}
		}

		if (resolvedMapId.has_value())
		{
			std::ostringstream details;
			details << "start_x=" << start.x
				<< " start_y=" << start.y
				<< " start_z=" << start.z
				<< " end_x=" << end.x
				<< " end_y=" << end.y
				<< " end_z=" << end.z
				<< " map_id=" << *resolvedMapId
				<< " map_directory=" << resolvedDirectory
				<< " candidate_count=" << candidateCount;
			LogDecisionOnce("repath", "map_inferred", details.str(), false);
		}

		return resolvedMapId;
	}

	std::shared_ptr<nav::Map> BotNavService::GetOrLoadMap(uint32 mapId, std::string& outDirectory, std::string& outReason)
	{
		outReason.clear();
		outDirectory.clear();

		if (const auto existing = m_loadedMaps.find(mapId); existing != m_loadedMaps.end())
		{
			if (const std::optional<std::string> resolvedDirectory = ResolveMapDirectory(mapId); resolvedDirectory.has_value())
			{
				outDirectory = *resolvedDirectory;
			}

			if (!existing->second->IsValid())
			{
				outReason = "nav_unavailable";
				return {};
			}

			return existing->second;
		}

		const std::optional<std::string> resolvedDirectory = ResolveMapDirectory(mapId);
		if (!resolvedDirectory.has_value())
		{
			outReason = "map_unresolved";
			return {};
		}

		outDirectory = *resolvedDirectory;
		const std::string mapFileName = outDirectory + ".map";
		if (!AssetRegistry::HasFile(mapFileName))
		{
			outReason = "nav_unavailable";
			return {};
		}

		// An unreadable .map is cached too, so it is reported once instead of reloaded on every query.
		auto loadedMap = std::make_shared<nav::Map>(outDirectory);
		m_loadedMaps.emplace(mapId, loadedMap);
		if (!loadedMap->IsValid())
		{
			outReason = "nav_unavailable";
			return {};
		}

		loadedMap->LoadAllPages();
		return loadedMap;
	}

	std::optional<bool> BotNavService::IsOnMesh(const uint32 mapId, const Vector3& position)
	{
		std::string mapDirectory;
		std::string reason;
		const std::shared_ptr<nav::Map> map = m_ready ? GetOrLoadMap(mapId, mapDirectory, reason) : nullptr;
		if (!map)
		{
			return std::nullopt;
		}

		// The same box nav::Map::FindPath searches for the polys under its start and end.
		constexpr float extents[] = { 5.0f, 3.5f, 5.0f };
		const float center[3] = { position.x, position.y, position.z };
		const dtQueryFilter filter;
		dtPolyRef poly = 0;
		const dtStatus status = map->GetNavMeshQuery().findNearestPoly(center, extents, &filter, &poly, nullptr);
		return dtStatusSucceed(status) && poly != 0;
	}

	BotPathResult BotNavService::FailPath(uint32 mapId, std::string mapDirectory, std::string reason, std::string details, const bool error)
	{
		BotPathResult result;
		result.mapId = mapId;
		result.mapDirectory = std::move(mapDirectory);
		result.reason = std::move(reason);

		std::ostringstream diagnostic;
		diagnostic << BuildMapDetails(result.mapId, result.mapDirectory);
		if (!details.empty())
		{
			diagnostic << ' ' << details;
		}

		LogDecisionOnce("abort", result.reason, diagnostic.str(), error);
		return result;
	}

	void BotNavService::LogDecisionOnce(std::string_view decision, std::string_view reason, std::string_view details, const bool error)
	{
		std::ostringstream keyBuilder;
		keyBuilder << decision << '|' << reason << '|' << details;
		const std::string key = keyBuilder.str();
		if (m_lastDecisionKey == key)
		{
			return;
		}

		m_lastDecisionKey = key;
		if (error)
		{
			ELOG("movement decision=" << decision << " reason=" << reason << ' ' << details);
		}
		else
		{
			WLOG("movement decision=" << decision << " reason=" << reason << ' ' << details);
		}
	}

	void BotNavService::ClearDecisionDedup()
	{
		m_lastDecisionKey.clear();
	}
}
