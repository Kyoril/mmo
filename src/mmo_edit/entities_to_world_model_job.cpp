// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "entities_to_world_model_job.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/writer.h"
#include "terrain/constants.h"
#include "game_common/world_entity_loader.h"
#include "log/default_log_levels.h"
#include "nlohmann/json.hpp"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_from_placements.h"
#include "scene_graph/world_model_serializer.h"

#include <cmath>
#include <fstream>
#include <sstream>

namespace mmo
{
	namespace
	{
		using json = nlohmann::json;

		/// @brief Page index of a world position, as the world editor names entity folders ((x << 8) | z).
		uint16 PageIndexOf(const float x, const float z)
		{
			const int32 pageX = static_cast<int32>(std::floor(x / terrain::constants::PageSize)) + 32;
			const int32 pageZ = static_cast<int32>(std::floor(z / terrain::constants::PageSize)) + 32;
			return static_cast<uint16>(((pageX & 0xff) << 8) | (pageZ & 0xff));
		}

		bool Matches(const String& path, const std::vector<String>& include, const std::vector<String>& exclude)
		{
			const auto contains = [&path](const String& needle) { return path.find(needle) != String::npos; };
			return (include.empty() || std::ranges::any_of(include, contains)) && !std::ranges::any_of(exclude, contains);
		}

		struct ConvertedEntity
		{
			String file;
			WorldModelPlacementSource placement;
		};
	}

	bool ApplyEntitiesToWorldModelJob(const String& jsonPath)
	{
		std::ifstream file(jsonPath, std::ios::binary);
		if (!file)
		{
			ELOG("Entities to world model: cannot open " << jsonPath);
			return false;
		}

		std::stringstream text;
		text << file.rdbuf();
		const json job = json::parse(text.str(), nullptr, false);
		if (job.is_discarded() || !job.is_object() || !job.contains("world") || !job.contains("pages") || !job.contains("output"))
		{
			ELOG("Entities to world model: " << jsonPath << " needs \"world\", \"pages\" and \"output\"");
			return false;
		}

		const String world = job["world"].get<String>();
		const std::vector<int32> pages = job["pages"].get<std::vector<int32>>();
		if (pages.size() != 4)
		{
			ELOG("Entities to world model: \"pages\" must be [x0, z0, x1, z1]");
			return false;
		}

		const std::vector<String> include = job.value("include", std::vector<String>());
		const std::vector<String> exclude = job.value("exclude", std::vector<String>());
		const float minRadius = job.value("minRadius", 0.0f);
		const float cellSize = job.value("cellSize", 64.0f);
		const String output = job["output"].get<String>();
		const bool dryRun = job.value("dryRun", false);

		std::vector<ConvertedEntity> converted;
		for (int32 pageX = pages[0]; pageX <= pages[2]; ++pageX)
		{
			for (int32 pageZ = pages[1]; pageZ <= pages[3]; ++pageZ)
			{
				const String folder = world + "/Entities/" + std::to_string((pageX << 8) | pageZ) + "/";
				for (const String& entityFile : AssetRegistry::ListFiles(folder, ".wobj"))
				{
					WorldEntityLoader loader;
					{
						const std::unique_ptr<std::istream> stream = AssetRegistry::OpenFile(entityFile);
						if (!stream)
						{
							ELOG("Entities to world model: cannot open " << entityFile);
							return false;
						}

						io::StreamSource source{ *stream };
						io::Reader reader{ source };
						if (!loader.Read(reader))
						{
							ELOG("Entities to world model: cannot read " << entityFile);
							return false;
						}
					}

					const WorldEntityLoader::MapEntity& entity = loader.GetEntity();
					if (entity.entityType != WorldEntityType::Mesh || !Matches(entity.meshName, include, exclude))
					{
						continue;
					}

					const MeshPtr mesh = MeshManager::Get().Load(entity.meshName);
					if (!mesh)
					{
						ELOG("Entities to world model: cannot load mesh " << entity.meshName << " of " << entityFile);
						return false;
					}

					AABB placed = mesh->GetBounds();
					placed.min *= entity.scale;
					placed.max *= entity.scale;
					if (GetBoundingRadiusFromAABB(placed) < minRadius)
					{
						continue;
					}

					ConvertedEntity item;
					item.file = entityFile;
					item.placement.meshPath = entity.meshName;
					item.placement.position = entity.position;
					item.placement.rotation = entity.rotation;
					item.placement.scale = entity.scale;
					item.placement.name = entity.name;
					item.placement.meshBounds = mesh->GetBounds();
					if (!entity.materialOverrides.empty())
					{
						// A mesh ref has one override for all its submeshes: take the first submesh's.
						item.placement.materialOverride = entity.materialOverrides.front().materialName;
						if (entity.materialOverrides.size() > 1 || entity.materialOverrides.front().materialIndex != 0)
						{
							WLOG("Entities to world model: " << entityFile << " overrides several submeshes; only "
								<< item.placement.materialOverride << " is kept");
						}
					}

					converted.push_back(std::move(item));
				}
			}
		}

		if (converted.empty())
		{
			ELOG("Entities to world model: no entity matched");
			return false;
		}

		Vector3 origin;
		if (job.contains("origin"))
		{
			const std::vector<float> o = job["origin"].get<std::vector<float>>();
			origin = Vector3(o.at(0), o.at(1), o.at(2));
		}
		else
		{
			for (const ConvertedEntity& item : converted)
			{
				origin += item.placement.position;
			}
			origin /= static_cast<float>(converted.size());
		}

		std::vector<WorldModelPlacementSource> placements;
		placements.reserve(converted.size());
		for (const ConvertedEntity& item : converted)
		{
			placements.push_back(item.placement);
		}

		WorldModel model(output);
		const size_t groups = AddWorldModelGroupsFromPlacements(model, placements, origin, cellSize);
		ILOG("Entities to world model: " << converted.size() << " entities in " << groups << " groups around ("
			<< origin.x << ", " << origin.y << ", " << origin.z << ") -> " << output);
		if (dryRun)
		{
			return true;
		}

		{
			const std::unique_ptr<std::ostream> out = AssetRegistry::CreateNewFile(output);
			if (!out)
			{
				ELOG("Entities to world model: cannot write " << output);
				return false;
			}

			io::StreamSink sink{ *out };
			io::Writer writer{ sink };
			WorldModelSerializer serializer;
			serializer.Serialize(model, writer);
			sink.Flush();
		}

		WorldEntityLoader::MapEntity placement;
		placement.entityType = WorldEntityType::WorldModel;
		placement.meshName = output;
		placement.position = origin;
		placement.rotation = Quaternion::Identity;
		placement.scale = Vector3::UnitScale;
		placement.uniqueId = GenerateUniqueId();
		placement.name = job.value("name", String());
		placement.category = job.value("category", String());

		const String placementFile = world + "/Entities/" + std::to_string(PageIndexOf(origin.x, origin.z)) + "/" + std::to_string(placement.uniqueId) + ".wobj";
		{
			const std::unique_ptr<std::ostream> out = AssetRegistry::CreateNewFile(placementFile);
			if (!out)
			{
				ELOG("Entities to world model: cannot write placement " << placementFile);
				return false;
			}

			io::StreamSink sink{ *out };
			io::Writer writer{ sink };
			WriteWorldModelPlacement(writer, placement);
			sink.Flush();
		}

		size_t removed = 0;
		for (const ConvertedEntity& item : converted)
		{
			if (AssetRegistry::RemoveFile(item.file))
			{
				++removed;
			}
			else
			{
				WLOG("Entities to world model: could not delete converted entity " << item.file);
			}
		}

		ILOG("Entities to world model: placed " << placementFile << ", removed " << removed << " converted entities");
		return removed == converted.size();
	}
}
