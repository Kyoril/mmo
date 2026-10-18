// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_nav.h"

#include "console/console_var.h"

#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "nav_mesh/map.h"
#include "terrain/constants.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace mmo
{
	namespace
	{
		/// Margin (yards) around a path's endpoints whose pages are loaded before the query. The
		/// world server uses the same margin for its on-demand nav loading.
		constexpr float PathPageMargin = 64.0f;

		/// Extra margin (yards) around a random-point search circle.
		constexpr float RandomPointPageMargin = 5.0f;

		constexpr int32 MaxPages = static_cast<int32>(terrain::constants::MaxPages);

		/// Copy of WorldToPage in nav_mesh/page_streamer.cpp: the nav mesh origin sits at -32
		/// pages on both axes, page x runs along world X and page y along world Z.
		int32 WorldToPage(const float coordinate)
		{
			constexpr double origin = -32.0 * terrain::constants::PageSize;
			const double page = std::floor((coordinate - origin) / terrain::constants::PageSize);
			return static_cast<int32>(std::clamp(page, 0.0, static_cast<double>(MaxPages - 1)));
		}

		bool IsFinite(const Vector3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		/// The asset registry is process-wide, so the mount is too.
		bool s_navMounted = false;
		std::filesystem::path s_lastMountAttempt;

		std::filesystem::path ResolveNavPath()
		{
			const ConsoleVar* dataPath = ConsoleVarMgr::FindConsoleVar("dataPath");
			const ConsoleVar* navPath = ConsoleVarMgr::FindConsoleVar("botNavPath");
			if (!dataPath || !navPath)
			{
				return {};
			}

			return (std::filesystem::absolute(dataPath->GetStringValue()) / navPath->GetStringValue()).lexically_normal();
		}

		/// Mounts the nav folder once. A missing folder is retried only after botNavPath changes,
		/// so a client without nav data warns once instead of on every query.
		bool EnsureNavMounted()
		{
			if (s_navMounted)
			{
				return true;
			}

			const std::filesystem::path navPath = ResolveNavPath();
			if (navPath.empty() || navPath == s_lastMountAttempt)
			{
				return false;
			}

			s_lastMountAttempt = navPath;

			std::error_code error;
			if (!std::filesystem::is_directory(navPath, error))
			{
				WLOG("Bot API: nav data folder " << navPath.generic_string() << " not found, bots have no navmesh");
				return false;
			}

			AssetRegistry::AddArchivePackage(navPath);
			s_navMounted = true;
			ILOG("Bot API: mounted nav data from " << navPath.generic_string());
			return true;
		}
	}

	BotNav::BotNav(const proto_client::MapManager& maps)
		: m_maps(maps)
	{
	}

	BotNav::~BotNav() = default;

	BotNavResult BotNav::FindPath(const uint32 mapId, const Vector3& from, const Vector3& to, std::vector<Vector3>& out)
	{
		out.clear();

		if (!EnsureMap(mapId))
		{
			return BotNavResult::NoNavmesh;
		}

		EnsurePages(from, to, PathPageMargin);

		if (!m_map->FindPath(from, to, out, true) || out.size() < 2)
		{
			out.clear();
			return BotNavResult::NoPath;
		}

		return BotNavResult::Ok;
	}

	BotNavResult BotNav::FindRandomPoint(const uint32 mapId, const Vector3& center, const float radius, Vector3& out)
	{
		if (!EnsureMap(mapId))
		{
			return BotNavResult::NoNavmesh;
		}

		EnsurePages(center, center, radius + RandomPointPageMargin);

		return m_map->FindRandomPointAroundCircle(center, radius, out) ? BotNavResult::Ok : BotNavResult::NoPath;
	}

	bool BotNav::EnsureMap(const uint32 mapId)
	{
		if (!EnsureNavMounted())
		{
			return false;
		}

		if (mapId == m_mapId && (m_map || m_mapUnavailable))
		{
			return m_map != nullptr;
		}

		m_map.reset();
		m_mapId = mapId;
		m_mapUnavailable = true;

		const proto_client::MapEntry* entry = m_maps.getById(mapId);
		if (!entry || entry->directory().empty())
		{
			WLOG("Bot API: map " << mapId << " is unknown, bots have no navmesh there");
			return false;
		}

		if (!AssetRegistry::HasFile(entry->directory() + ".map"))
		{
			WLOG("Bot API: no nav data for map " << entry->directory() << ", bots have no navmesh there");
			return false;
		}

		auto map = std::make_unique<nav::Map>(entry->directory());
		if (!map->IsValid())
		{
			return false;
		}

		m_map = std::move(map);
		m_mapUnavailable = false;
		return true;
	}

	void BotNav::EnsurePages(const Vector3& a, const Vector3& b, const float margin)
	{
		if (!m_map->HasPages())
		{
			return;
		}

		// A broken position must not turn into page indices and from there into a synchronous
		// load of half the map.
		if (!IsFinite(a) || !IsFinite(b) || !std::isfinite(margin))
		{
			return;
		}

		const int32 firstX = WorldToPage(std::min(a.x, b.x) - margin);
		const int32 lastX = WorldToPage(std::max(a.x, b.x) + margin);
		const int32 firstY = WorldToPage(std::min(a.z, b.z) - margin);
		const int32 lastY = WorldToPage(std::max(a.z, b.z) + margin);

		for (int32 y = firstY; y <= lastY; ++y)
		{
			for (int32 x = firstX; x <= lastX; ++x)
			{
				if (m_map->HasPage(x, y) && !m_map->IsPageLoaded(x, y))
				{
					m_map->LoadPage(x, y);
				}
			}
		}
	}
}
