// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"
#include "mmo_bot/bot_nav_service.h"

#include <filesystem>
#include <limits>
#include <optional>
#include <vector>

namespace mmo
{
	namespace
	{
		struct NavigableSample final
		{
			uint32 mapId { 0 };
			std::string mapDirectory;
			Vector3 start;
			Vector3 end;
		};

		void AddCandidate(std::vector<Vector3>& points, const Vector3& candidate)
		{
			for (const Vector3& existing : points)
			{
				if ((existing - candidate).GetSquaredLength() < 0.25f)
				{
					return;
				}
			}

			points.push_back(candidate);
		}

		std::vector<Vector3> GatherCandidatePoints(const proto::MapEntry& mapEntry)
		{
			std::vector<Vector3> points;

			for (int i = 0; i < mapEntry.unitspawns_size(); ++i)
			{
				const auto& spawn = mapEntry.unitspawns(i);
				AddCandidate(points, Vector3(spawn.positionx(), spawn.positiony(), spawn.positionz()));
				for (int locationIndex = 0; locationIndex < spawn.locations_size(); ++locationIndex)
				{
					const auto& location = spawn.locations(locationIndex);
					AddCandidate(points, Vector3(location.positionx(), location.positiony(), location.positionz()));
				}
			}

			for (int i = 0; i < mapEntry.objectspawns_size(); ++i)
			{
				const auto& location = mapEntry.objectspawns(i).location();
				AddCandidate(points, Vector3(location.positionx(), location.positiony(), location.positionz()));
			}

			return points;
		}

		std::optional<NavigableSample> FindNavigableSample(BotNavService& service)
		{
			const proto::Project* project = service.GetProject();
			if (!project)
			{
				return std::nullopt;
			}

			const auto& maps = project->maps.getTemplates();
			for (int mapIndex = 0; mapIndex < maps.entry_size(); ++mapIndex)
			{
				const auto& mapEntry = maps.entry(mapIndex);
				if (mapEntry.directory().empty())
				{
					continue;
				}

				if (!std::filesystem::exists(service.GetNavDataPath() / (mapEntry.directory() + ".map")))
				{
					continue;
				}

				const std::vector<Vector3> points = GatherCandidatePoints(mapEntry);
				for (size_t startIndex = 0; startIndex < points.size(); ++startIndex)
				{
					for (size_t endIndex = startIndex + 1; endIndex < points.size(); ++endIndex)
					{
						BotPathResult result = service.FindPath(mapEntry.id(), points[startIndex], points[endIndex]);
						if (result.success)
						{
							return NavigableSample {
								mapEntry.id(),
								mapEntry.directory(),
								points[startIndex],
								points[endIndex],
							};
						}
					}
				}
			}

			return std::nullopt;
		}
	}

	TEST_CASE("Bot nav service loads committed repo project data", "[bot-nav]")
	{
		BotNavService service;

		REQUIRE(service.IsReady());
		REQUIRE(service.GetProject() != nullptr);
		REQUIRE(std::filesystem::exists(service.GetProjectDataPath() / "project.txt"));
		REQUIRE(service.GetProject()->maps.count() > 0);
	}

	TEST_CASE("Bot nav service rejects missing repo project roots conservatively", "[bot-nav]")
	{
		BotNavService service(std::filesystem::path("__missing_repo_root__"));

		REQUIRE_FALSE(service.IsReady());
		REQUIRE(service.GetFailureReason() == "project_load_failed");

		const BotPathResult result = service.FindPath(1, Vector3::Zero, Vector3::UnitX);
		REQUIRE_FALSE(result.success);
		REQUIRE(result.reason == "project_load_failed");
	}

	TEST_CASE("Bot nav service resolves committed map directories and produces a real path", "[bot-nav]")
	{
		BotNavService service;
		REQUIRE(service.IsReady());

		const std::optional<NavigableSample> sample = FindNavigableSample(service);
		REQUIRE(sample.has_value());

		const std::optional<std::string> resolvedDirectory = service.ResolveMapDirectory(sample->mapId);
		REQUIRE(resolvedDirectory.has_value());
		REQUIRE(*resolvedDirectory == sample->mapDirectory);

		const BotPathResult result = service.FindPath(sample->mapId, sample->start, sample->end);
		REQUIRE(result.success);
		REQUIRE(result.reason.empty());
		REQUIRE(result.points.size() >= 2);
		REQUIRE(result.mapDirectory == sample->mapDirectory);
		REQUIRE(service.GetLoadedMapCount() >= 1);
	}

	TEST_CASE("Bot nav service can infer a usable map id from navigable positions", "[bot-nav]")
	{
		BotNavService service;
		REQUIRE(service.IsReady());

		const std::optional<NavigableSample> sample = FindNavigableSample(service);
		REQUIRE(sample.has_value());

		const std::optional<uint32> inferredMapId = service.InferMapId(sample->start, sample->end);
		REQUIRE(inferredMapId.has_value());

		const BotPathResult result = service.FindPath(*inferredMapId, sample->start, sample->end);
		REQUIRE(result.success);
		CHECK(result.points.size() >= 2);
	}

	TEST_CASE("Bot nav service reports unknown map ids explicitly", "[bot-nav]")
	{
		BotNavService service;
		REQUIRE(service.IsReady());

		const BotPathResult result = service.FindPath(std::numeric_limits<uint32>::max(), Vector3::Zero, Vector3::UnitX);
		REQUIRE_FALSE(result.success);
		REQUIRE(result.reason == "map_unresolved");
	}

	TEST_CASE("Bot nav service reports unpathable requests explicitly", "[bot-nav]")
	{
		BotNavService service;
		REQUIRE(service.IsReady());

		const std::optional<NavigableSample> sample = FindNavigableSample(service);
		REQUIRE(sample.has_value());

		const Vector3 impossibleDestination = sample->start + Vector3(100000.0f, 5000.0f, 100000.0f);
		const BotPathResult result = service.FindPath(sample->mapId, sample->start, impossibleDestination);
		REQUIRE_FALSE(result.success);
		REQUIRE(result.reason == "invalid_path");
		REQUIRE(result.mapDirectory == sample->mapDirectory);
	}
}
