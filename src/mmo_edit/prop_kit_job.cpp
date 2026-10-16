// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "prop_kit_job.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/writer.h"
#include "game_common/world_entity_loader.h"
#include "log/default_log_levels.h"
#include "nlohmann/json.hpp"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/prop_kit.h"
#include "scene_graph/prop_kit_checks.h"
#include "scene_graph/prop_kit_serializer.h"
#include "scene_graph/world_model.h"
#include "scene_graph/world_model_serializer.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace mmo
{
	namespace
	{
		using json = nlohmann::json;

		bool readString(const json& object, const char* key, String& out, const bool required)
		{
			const auto it = object.find(key);
			if (it == object.end() || it->is_null())
			{
				if (required)
				{
					ELOG("Prop kit job: missing '" << key << "'");
				}

				return !required;
			}

			if (!it->is_string())
			{
				ELOG("Prop kit job: '" << key << "' must be a string");
				return false;
			}

			out = it->get<String>();
			return true;
		}

		bool readFloatArray(const json& value, const size_t count, float* out)
		{
			if (!value.is_array() || value.size() != count)
			{
				return false;
			}

			for (size_t i = 0; i < count; ++i)
			{
				if (!value[i].is_number())
				{
					return false;
				}

				out[i] = value[i].get<float>();
			}

			return true;
		}

		bool readVector3(const json& object, const char* key, Vector3& out)
		{
			const auto it = object.find(key);
			if (it == object.end())
			{
				return true;
			}

			float values[3];
			if (!readFloatArray(*it, 3, values))
			{
				ELOG("Prop kit job: '" << key << "' must be an array of 3 numbers");
				return false;
			}

			out = Vector3(values[0], values[1], values[2]);
			return true;
		}

		bool readFloat(const json& object, const char* key, float& out)
		{
			const auto it = object.find(key);
			if (it == object.end())
			{
				return true;
			}

			if (!it->is_number())
			{
				ELOG("Prop kit job: '" << key << "' must be a number");
				return false;
			}

			out = it->get<float>();
			return true;
		}

		bool readQuaternion(const json& value, const char* key, Quaternion& out)
		{
			float q[4];
			if (!readFloatArray(value, 4, q))
			{
				ELOG("Prop kit job: '" << key << "' must be an array of 4 numbers [w,x,y,z]");
				return false;
			}

			out = Quaternion(q[0], q[1], q[2], q[3]);
			return true;
		}

		bool parseProp(const json& entry, WorldModelMeshRef& prop)
		{
			if (!entry.is_object())
			{
				ELOG("Prop kit job: props must be objects");
				return false;
			}

			if (!readString(entry, "mesh", prop.meshPath, true) || prop.meshPath.empty())
			{
				ELOG("Prop kit job: every prop needs a 'mesh'");
				return false;
			}

			if (!readString(entry, "name", prop.name, false) || !readString(entry, "material", prop.materialOverride, false))
			{
				return false;
			}

			if (!readVector3(entry, "position", prop.position))
			{
				return false;
			}

			// 'rotation' and 'yaw' are alternatives, 'rotation' wins
			const auto rotationIt = entry.find("rotation");
			const auto yawIt = entry.find("yaw");
			if (rotationIt != entry.end())
			{
				if (!readQuaternion(*rotationIt, "rotation", prop.rotation))
				{
					return false;
				}
			}
			else if (yawIt != entry.end())
			{
				if (!yawIt->is_number())
				{
					ELOG("Prop kit job: 'yaw' must be a number");
					return false;
				}

				prop.rotation = Quaternion(Radian(Degree(yawIt->get<float>())), Vector3::UnitY);
			}

			const auto scaleIt = entry.find("scale");
			if (scaleIt != entry.end())
			{
				if (scaleIt->is_number())
				{
					const float s = scaleIt->get<float>();
					prop.scale = Vector3(s, s, s);
				}
				else
				{
					float values[3];
					if (!readFloatArray(*scaleIt, 3, values))
					{
						ELOG("Prop kit job: 'scale' must be a number or an array of 3 numbers");
						return false;
					}

					prop.scale = Vector3(values[0], values[1], values[2]);
				}
			}

			const auto visibleIt = entry.find("visible");
			if (visibleIt != entry.end())
			{
				if (!visibleIt->is_boolean())
				{
					ELOG("Prop kit job: 'visible' must be a boolean");
					return false;
				}

				prop.visible = visibleIt->get<bool>();
			}

			return true;
		}

		bool parseLight(const json& entry, WorldModelLight& light)
		{
			if (!entry.is_object())
			{
				ELOG("Prop kit job: lights must be objects");
				return false;
			}

			light = WorldModelLight();
			light.type = WorldModelLight::LightType::Omni;
			light.useAttenuation = true;
			light.color = 0xFFFFFFFF;
			light.position = Vector3::Zero;
			light.intensity = 1.0f;
			light.rotation = Quaternion::Identity;
			light.attenuationStart = 0.0f;
			light.attenuationEnd = 10.0f;

			String type = "omni";
			if (!readString(entry, "type", type, false))
			{
				return false;
			}

			if (type == "omni")
			{
				light.type = WorldModelLight::LightType::Omni;
			}
			else if (type == "spot")
			{
				light.type = WorldModelLight::LightType::Spot;
			}
			else if (type == "direct")
			{
				light.type = WorldModelLight::LightType::Direct;
			}
			else if (type == "ambient")
			{
				light.type = WorldModelLight::LightType::Ambient;
			}
			else
			{
				ELOG("Prop kit job: unknown light type '" << type << "'");
				return false;
			}

			String color;
			if (!readString(entry, "color", color, false))
			{
				return false;
			}

			if (!color.empty())
			{
				if (color.size() > 8 || color.find_first_not_of("0123456789abcdefABCDEF") != String::npos)
				{
					ELOG("Prop kit job: light color '" << color << "' must be up to 8 hex digits");
					return false;
				}

				light.color = static_cast<uint32>(std::stoul(color, nullptr, 16));
			}

			float range = light.attenuationEnd;
			if (!readVector3(entry, "position", light.position) ||
				!readFloat(entry, "intensity", light.intensity) ||
				!readFloat(entry, "range", range) ||
				!readFloat(entry, "innerCone", light.innerConeAngle) ||
				!readFloat(entry, "outerCone", light.outerConeAngle) ||
				!readFloat(entry, "fogScattering", light.fogScattering))
			{
				return false;
			}

			light.attenuationEnd = range;

			const auto rotationIt = entry.find("rotation");
			if (rotationIt != entry.end() && !readQuaternion(*rotationIt, "rotation", light.rotation))
			{
				return false;
			}

			return true;
		}

		bool loadWorldModel(const String& path, WorldModel& model)
		{
			const std::unique_ptr<std::istream> stream = AssetRegistry::OpenFile(path);
			if (!stream)
			{
				ELOG("Prop kit job: cannot open world model " << path);
				return false;
			}

			io::StreamSource source{ *stream };
			io::Reader reader{ source };
			WorldModelDeserializer deserializer(model);
			if (!deserializer.Read(reader))
			{
				ELOG("Prop kit job: cannot read world model " << path);
				return false;
			}

			return true;
		}

		/// Collision bounds of a mesh from its collision tree, nothing for meshes without collision.
		std::optional<AABB> collisionBoundsOfMesh(std::map<String, std::optional<AABB>>& cache, const String& meshPath)
		{
			const auto it = cache.find(meshPath);
			if (it != cache.end())
			{
				return it->second;
			}

			std::optional<AABB> bounds;
			if (const MeshPtr mesh = MeshManager::Get().Load(meshPath))
			{
				const AABBTree& tree = mesh->GetCollisionTree();
				if (!tree.IsEmpty())
				{
					bounds = tree.GetBoundingBox();
				}
			}
			else
			{
				WLOG("Prop kit job: cannot load mesh " << meshPath << " for the clearance check");
			}

			cache[meshPath] = bounds;
			return bounds;
		}

		bool writeKit(const String& path, const PropKit& kit)
		{
			const std::unique_ptr<std::ostream> out = AssetRegistry::CreateNewFile(path);
			if (!out)
			{
				ELOG("Prop kit job: cannot write kit " << path);
				return false;
			}

			io::StreamSink sink{ *out };
			io::Writer writer{ sink };
			PropKitSerializer serializer;
			serializer.Serialize(kit, writer);
			sink.Flush();

			ILOG("Prop kit job: wrote kit " << path << " (" << kit.props.size() << " props, " << kit.lights.size() << " lights)");
			return true;
		}

		/// Finds the placement file of a unique id below the world's entity folders.
		String findPlacementFile(const String& world, const String& uniqueId)
		{
			const String prefix = "Worlds/" + world + "/" + world + "/Entities/";
			const String suffix = "/" + uniqueId + ".wobj";
			for (const String& file : AssetRegistry::ListFiles(prefix, ".wobj"))
			{
				if (file.ends_with(suffix))
				{
					return file;
				}
			}

			return String();
		}

		bool assignKits(const json& entry)
		{
			String world;
			if (!entry.is_object() || !readString(entry, "world", world, true) || world.empty())
			{
				ELOG("Prop kit job: every assign entry needs a 'world'");
				return false;
			}

			// The id is a decimal string, 64 bit ids do not survive json numbers
			const auto idIt = entry.find("uniqueId");
			if (idIt == entry.end() || !idIt->is_string() || idIt->get<String>().empty())
			{
				ELOG("Prop kit job: every assign entry needs a 'uniqueId' string");
				return false;
			}

			const String uniqueId = idIt->get<String>();
			if (uniqueId.find_first_not_of("0123456789") != String::npos)
			{
				ELOG("Prop kit job: uniqueId '" << uniqueId << "' must consist of digits only");
				return false;
			}

			std::vector<String> kits;
			const auto kitsIt = entry.find("kits");
			if (kitsIt == entry.end() || !kitsIt->is_array())
			{
				ELOG("Prop kit job: every assign entry needs a 'kits' array");
				return false;
			}

			for (const json& kitPath : *kitsIt)
			{
				if (!kitPath.is_string())
				{
					ELOG("Prop kit job: kit paths must be strings");
					return false;
				}

				kits.push_back(kitPath.get<String>());
				if (!AssetRegistry::HasFile(kits.back()))
				{
					WLOG("Prop kit job: assigned kit " << kits.back() << " does not exist");
				}
			}

			const String file = findPlacementFile(world, uniqueId);
			if (file.empty())
			{
				ELOG("Prop kit job: no placement " << uniqueId << " found in world " << world);
				return false;
			}

			WorldEntityLoader loader;
			{
				const std::unique_ptr<std::istream> stream = AssetRegistry::OpenFile(file);
				if (!stream)
				{
					ELOG("Prop kit job: cannot open placement " << file);
					return false;
				}

				io::StreamSource source{ *stream };
				io::Reader reader{ source };
				if (!loader.Read(reader))
				{
					ELOG("Prop kit job: cannot read placement " << file);
					return false;
				}
			}

			WorldEntityLoader::MapEntity placement = loader.GetEntity();
			if (placement.entityType != WorldEntityType::WorldModel)
			{
				ELOG("Prop kit job: placement " << file << " is not a world model");
				return false;
			}

			placement.kitPaths = kits;

			const std::unique_ptr<std::ostream> out = AssetRegistry::CreateNewFile(file);
			if (!out)
			{
				ELOG("Prop kit job: cannot write placement " << file);
				return false;
			}

			io::StreamSink sink{ *out };
			io::Writer writer{ sink };
			WriteWorldModelPlacement(writer, placement);
			sink.Flush();

			ILOG("Prop kit job: assigned " << kits.size() << " kit(s) to " << file);
			return true;
		}

		bool removeMeshRefs(const json& entry)
		{
			String path;
			if (!readString(entry, "worldModel", path, true) || path.empty())
			{
				ELOG("Prop kit job: removeMeshRefs needs a 'worldModel'");
				return false;
			}

			std::set<String> names;
			const auto namesIt = entry.find("names");
			if (namesIt != entry.end())
			{
				if (!namesIt->is_array())
				{
					ELOG("Prop kit job: removeMeshRefs 'names' must be an array");
					return false;
				}

				for (const json& name : *namesIt)
				{
					if (!name.is_string())
					{
						ELOG("Prop kit job: removeMeshRefs names must be strings");
						return false;
					}

					names.insert(name.get<String>());
				}
			}

			bool removeLights = false;
			const auto lightsIt = entry.find("removeLights");
			if (lightsIt != entry.end())
			{
				if (!lightsIt->is_boolean())
				{
					ELOG("Prop kit job: removeMeshRefs 'removeLights' must be a boolean");
					return false;
				}

				removeLights = lightsIt->get<bool>();
			}

			// Read straight from disk, a cached manager instance could be stale
			WorldModel model;
			if (!loadWorldModel(path, model))
			{
				return false;
			}

			const MeshRefRemoval removal = RemoveMeshRefsByName(model, names);
			for (const String& name : removal.unmatchedNames)
			{
				WLOG("Prop kit job: no mesh ref named '" << name << "' in " << path);
			}

			const bool clearsLights = removeLights && !model.GetLights().empty();
			if (clearsLights)
			{
				ILOG("Prop kit job: clearing " << model.GetLights().size() << " light(s) of " << path);
				model.GetLights().clear();
			}

			ILOG("Prop kit job: removed " << removal.removed << " mesh ref(s) from " << path);

			// A re-run finds nothing left to remove: leave the file untouched.
			if (removal.removed == 0 && !clearsLights)
			{
				ILOG("Prop kit job: " << path << " is unchanged, not rewriting it");
				return true;
			}

			// As the world model editor's Save does
			model.RecalculateBoundingBox();
			const std::unique_ptr<std::ostream> out = AssetRegistry::CreateNewFile(path);
			if (!out)
			{
				ELOG("Prop kit job: cannot write world model " << path);
				return false;
			}

			io::StreamSink sink{ *out };
			io::Writer writer{ sink };
			WorldModelSerializer serializer;
			serializer.Serialize(model, writer);
			sink.Flush();
			return true;
		}
	}

	bool ApplyPropKitJob(const String& jsonPath)
	{
		std::ifstream file(jsonPath, std::ios::binary);
		if (!file)
		{
			ELOG("Prop kit job: cannot open " << jsonPath);
			return false;
		}

		std::stringstream text;
		text << file.rdbuf();

		const json job = json::parse(text.str(), nullptr, false);
		if (job.is_discarded() || !job.is_object())
		{
			ELOG("Prop kit job: " << jsonPath << " is not a valid JSON object");
			return false;
		}

		// Validate and build everything before touching any asset
		String kitPath;
		PropKit kit;
		bool hasKit = false;
		if (job.contains("kit"))
		{
			if (!readString(job, "kit", kitPath, true) || kitPath.empty() ||
				!readString(job, "name", kit.name, false) || !readString(job, "preview", kit.previewWorldModel, false))
			{
				return false;
			}

			hasKit = true;

			const auto propsIt = job.find("props");
			if (propsIt != job.end())
			{
				if (!propsIt->is_array())
				{
					ELOG("Prop kit job: 'props' must be an array");
					return false;
				}

				for (const json& entry : *propsIt)
				{
					WorldModelMeshRef prop;
					if (!parseProp(entry, prop))
					{
						return false;
					}

					kit.props.push_back(std::move(prop));
				}
			}

			const auto lightsIt = job.find("lights");
			if (lightsIt != job.end())
			{
				if (!lightsIt->is_array())
				{
					ELOG("Prop kit job: 'lights' must be an array");
					return false;
				}

				for (const json& entry : *lightsIt)
				{
					WorldModelLight light;
					if (!parseLight(entry, light))
					{
						return false;
					}

					kit.lights.push_back(light);
				}
			}
		}

		const auto assignIt = job.find("assign");
		if (assignIt != job.end() && !assignIt->is_array())
		{
			ELOG("Prop kit job: 'assign' must be an array");
			return false;
		}

		const auto removeIt = job.find("removeMeshRefs");
		if (removeIt != job.end() && !removeIt->is_object())
		{
			ELOG("Prop kit job: 'removeMeshRefs' must be an object");
			return false;
		}

		bool success = true;

		if (hasKit)
		{
			// Doorway clearance against the preview world model: warnings only
			if (!kit.previewWorldModel.empty())
			{
				WorldModel preview;
				if (loadWorldModel(kit.previewWorldModel, preview))
				{
					std::map<String, std::optional<AABB>> boundsCache;
					const auto issues = FindPropsNearPortals(preview, kit,
						[&boundsCache](const String& meshPath) { return collisionBoundsOfMesh(boundsCache, meshPath); });
					for (const PortalClearanceIssue& issue : issues)
					{
						WLOG("Prop kit job: prop '" << issue.propName << "' is " << issue.distance << " m from portal " << issue.portalIndex << " of " << kit.previewWorldModel);
					}
				}
				else
				{
					WLOG("Prop kit job: skipped the doorway check, preview world model could not be loaded");
				}
			}

			success = writeKit(kitPath, kit) && success;
		}

		if (assignIt != job.end())
		{
			for (const json& entry : *assignIt)
			{
				success = assignKits(entry) && success;
			}
		}

		if (removeIt != job.end())
		{
			success = removeMeshRefs(*removeIt) && success;
		}

		return success;
	}
}
