// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_nav_service.h"

#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "nav_mesh/map.h"

#include <limits>
#include <sstream>

namespace fs = std::filesystem;

namespace mmo
{
	namespace
	{
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

	BotNavService::BotNavService(fs::path repoRoot)
		: m_repoRoot(std::move(repoRoot))
	{
		static_cast<void>(Initialize());
	}

	BotNavService::~BotNavService()
	{
		m_loadedMaps.clear();
		if (m_assetRegistryInitialized)
		{
			AssetRegistry::Destroy();
		}
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

	BotPathResult BotNavService::FindPath(const uint32 mapId, const Vector3& start, const Vector3& end)
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
		if (!map->FindPath(start, end, result.points, true))
		{
			return FailPath(mapId, std::move(mapDirectory), "invalid_path", "phase=find_path status=query_failed", false);
		}

		if (result.points.size() < 2)
		{
			return FailPath(mapId, std::move(mapDirectory), "invalid_path", "phase=find_path status=malformed_path point_count=" + std::to_string(result.points.size()), false);
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

		auto loadedMap = std::make_shared<nav::Map>(outDirectory);
		loadedMap->LoadAllPages();
		m_loadedMaps.emplace(mapId, loadedMap);
		return loadedMap;
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
