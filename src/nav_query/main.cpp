// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "assets/asset_registry.h"
#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "math/vector3.h"
#include "nav_mesh/map.h"

#include "cxxopts/cxxopts.hpp"
#include <nlohmann/json.hpp>

#include "DetourNavMeshQuery.h"

#include <iostream>
#include <string>
#include <vector>

namespace mmo
{
	namespace
	{
		/// Reads an [x, y, z] number array; returns false when the value has another shape.
		bool readPoint(const nlohmann::json& value, Vector3& outPoint)
		{
			if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() || !value[2].is_number())
			{
				return false;
			}

			outPoint = Vector3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
			return true;
		}

		nlohmann::json writePoint(const Vector3& point)
		{
			return nlohmann::json::array({ point.x, point.y, point.z });
		}

		nlohmann::json failure(const char* reason)
		{
			return { { "ok", false }, { "error", reason } };
		}

		nlohmann::json pathQuery(const nav::Map& map, const nlohmann::json& request)
		{
			Vector3 from;
			Vector3 to;
			if (!request.contains("from") || !request.contains("to") || !readPoint(request["from"], from) || !readPoint(request["to"], to))
			{
				return failure("path needs from and to as [x, y, z]");
			}

			std::vector<Vector3> points;
			if (!map.FindPath(from, to, points, false) || points.size() < 2)
			{
				return { { "ok", false } };
			}

			float length = 0.0f;
			nlohmann::json out = { { "ok", true }, { "points", nlohmann::json::array() } };
			for (size_t i = 0; i < points.size(); ++i)
			{
				if (i > 0)
				{
					length += (points[i] - points[i - 1]).GetLength();
				}

				out["points"].push_back(writePoint(points[i]));
			}

			out["length"] = length;
			return out;
		}

		nlohmann::json onMeshQuery(const nav::Map& map, const nlohmann::json& request)
		{
			Vector3 at;
			if (!request.contains("at") || !readPoint(request["at"], at))
			{
				return failure("on_mesh needs at as [x, y, z]");
			}

			const float radius = request.contains("radius") && request["radius"].is_number() ? request["radius"].get<float>() : 2.0f;
			const float center[3] = { at.x, at.y, at.z };
			const float extents[3] = { radius, 5.0f, radius };
			dtQueryFilter filter;
			dtPolyRef polygon = 0;
			float nearest[3] = { 0.0f, 0.0f, 0.0f };
			if (dtStatusFailed(map.GetNavMeshQuery().findNearestPoly(center, extents, &filter, &polygon, nearest)) || polygon == 0)
			{
				return { { "ok", false } };
			}

			const Vector3 point(nearest[0], nearest[1], nearest[2]);
			return { { "ok", true }, { "nearest", writePoint(point) }, { "distance", (point - at).GetLength() } };
		}

		nlohmann::json answer(const nav::Map& map, const std::string& line)
		{
			const nlohmann::json request = nlohmann::json::parse(line, nullptr, false);
			if (request.is_discarded() || !request.is_object() || !request.contains("op") || !request["op"].is_string())
			{
				return failure("expected a JSON object with an op");
			}

			const std::string op = request["op"].get<std::string>();
			if (op == "path")
			{
				return pathQuery(map, request);
			}

			if (op == "on_mesh")
			{
				return onMeshQuery(map, request);
			}

			return failure("unknown op");
		}
	}
}

/// Answers navmesh queries (JSON lines on stdin, one JSON line per answer on stdout) for tools/world.
int main(int argc, char* argv[])
{
	auto logOptions = mmo::g_DefaultConsoleLogOptions;
	mmo::g_DefaultLog.signal().connect([&logOptions](const mmo::LogEntry& entry)
	{
		printLogEntry(std::cerr, entry, logOptions);
	});

	std::string navDirectory;
	std::string worldName;

	cxxopts::Options options("nav_query", "Navmesh path and on-mesh queries as JSON lines");
	options.add_options()
		("n,nav", "directory holding <World>.map and <World>/XX_YY.nav", cxxopts::value<std::string>(navDirectory))
		("w,world", "world name", cxxopts::value<std::string>(worldName));

	try
	{
		options.parse(argc, argv);
	}
	catch (const cxxopts::OptionException& e)
	{
		ELOG(e.what());
		return 1;
	}

	if (navDirectory.empty() || worldName.empty())
	{
		ELOG("nav_query needs --nav and --world");
		return 1;
	}

	mmo::AssetRegistry::Initialize(navDirectory, {});
	if (!mmo::AssetRegistry::HasFile(worldName + ".map"))
	{
		ELOG("No " << worldName << ".map in " << navDirectory);
		mmo::AssetRegistry::Destroy();
		return 1;
	}

	int exitCode = 0;
	{
		mmo::nav::Map map(worldName);
		const mmo::int32 pages = map.LoadAllPages();
		if (pages <= 0)
		{
			ELOG("No navigation pages loaded for " << worldName);
			exitCode = 1;
		}
		else
		{
			std::cout << nlohmann::json({ { "ready", true }, { "pages", pages } }).dump() << std::endl;
			std::string line;
			while (std::getline(std::cin, line))
			{
				if (!line.empty())
				{
					std::cout << mmo::answer(map, line).dump() << std::endl;
				}
			}
		}
	}

	mmo::AssetRegistry::Destroy();
	return exitCode;
}
