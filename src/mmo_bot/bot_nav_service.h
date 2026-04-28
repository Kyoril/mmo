// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"
#include "proto_data/project.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mmo
{
	namespace nav
	{
		class Map;
	}

	struct BotPathResult final
	{
		bool success { false };
		uint32 mapId { 0 };
		std::string mapDirectory;
		std::string reason;
		std::vector<Vector3> points;
	};

	class BotNavService final
	{
	public:
		explicit BotNavService(std::filesystem::path repoRoot = {});
		~BotNavService();

		BotNavService(const BotNavService&) = delete;
		BotNavService& operator=(const BotNavService&) = delete;

	public:
		[[nodiscard]] bool IsReady() const noexcept { return m_ready; }
		[[nodiscard]] const std::string& GetFailureReason() const noexcept { return m_failureReason; }
		[[nodiscard]] const std::filesystem::path& GetRepoRoot() const noexcept { return m_repoRoot; }
		[[nodiscard]] const std::filesystem::path& GetProjectDataPath() const noexcept { return m_projectDataPath; }
		[[nodiscard]] const std::filesystem::path& GetNavDataPath() const noexcept { return m_navDataPath; }
		[[nodiscard]] const proto::Project* GetProject() const noexcept { return m_ready ? &m_project : nullptr; }
		[[nodiscard]] size_t GetLoadedMapCount() const noexcept { return m_loadedMaps.size(); }

		[[nodiscard]] std::optional<std::string> ResolveMapDirectory(uint32 mapId) const;
		[[nodiscard]] bool EnsureMapAvailable(uint32 mapId);
		[[nodiscard]] BotPathResult FindPath(uint32 mapId, const Vector3& start, const Vector3& end);

	private:
		[[nodiscard]] bool Initialize();
		[[nodiscard]] static std::optional<std::filesystem::path> ResolveRepoRoot(std::filesystem::path start);
		[[nodiscard]] std::shared_ptr<nav::Map> GetOrLoadMap(uint32 mapId, std::string& outDirectory, std::string& outReason);
		[[nodiscard]] BotPathResult FailPath(uint32 mapId, std::string mapDirectory, std::string reason, std::string details, bool error = false);
		void LogDecisionOnce(std::string_view decision, std::string_view reason, std::string_view details, bool error = false);
		void ClearDecisionDedup();

	private:
		std::filesystem::path m_repoRoot;
		std::filesystem::path m_projectDataPath;
		std::filesystem::path m_navDataPath;
		proto::Project m_project;
		std::unordered_map<uint32, std::shared_ptr<nav::Map>> m_loadedMaps;
		bool m_assetRegistryInitialized { false };
		bool m_ready { false };
		std::string m_failureReason;
		std::string m_lastDecisionKey;
	};
}
