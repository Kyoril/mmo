// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_bake.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>

namespace mmo
{
	namespace
	{
		using json = nlohmann::json;

		constexpr float PiF = 3.14159265358979323846f;

		/// Aspect ratios the bake has to look right at.
		constexpr std::array<float, 3> AspectRatios = { 4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f };

		/// Sideways drift of the gate pose in metres.
		constexpr float GateDrift = 0.3f;

		/// Eye height of a character walking the plaza in metres.
		constexpr float EyeHeight = 1.7f;

		/// Height above a spot the push-in looks at in metres.
		constexpr float PushInHeight = 1.4f;

		/// Fraction of the way towards a spot the push-in end pose travels.
		constexpr float PushInFraction = 0.4f;

		/// Number of poses between the entrance and the front spot.
		constexpr uint32 EntranceSamples = 8;

		constexpr uint32 InteriorFlag = 0x2000;
		constexpr uint32 ExteriorFlag = 0x8;

		Vector3 lerp(const Vector3& a, const Vector3& b, float t)
		{
			return a + (b - a) * t;
		}

		Vector3 rightOf(const Vector3& forward)
		{
			Vector3 up = Vector3::UnitY;
			if (std::fabs(forward.Dot(up)) > 0.999f)
			{
				up = Vector3::UnitZ;
			}

			Vector3 right = up.Cross(forward);
			right.Normalize();
			return right;
		}

		void addPushIn(std::vector<GlueCameraPose>& poses, const GlueCameraPose& from, const Vector3& spotPosition)
		{
			const Vector3 target = spotPosition + Vector3(0.0f, PushInHeight, 0.0f);
			poses.push_back({ lerp(from.position, target, PushInFraction), target });
		}

		// ---- JSON helpers ----

		json writeVector(const Vector3& v)
		{
			return json::array({ v.x, v.y, v.z });
		}

		json writeQuaternion(const Quaternion& q)
		{
			return json::array({ q.w, q.x, q.y, q.z });
		}

		json writeMatrix(const Matrix4& m)
		{
			json result = json::array();
			for (int row = 0; row < 4; ++row)
			{
				for (int col = 0; col < 4; ++col)
				{
					result.push_back(m[row][col]);
				}
			}
			return result;
		}

		bool readNumbers(const json& node, size_t count, float* out)
		{
			if (!node.is_array() || node.size() != count)
			{
				return false;
			}

			for (size_t i = 0; i < count; ++i)
			{
				if (!node[i].is_number())
				{
					return false;
				}
				out[i] = node[i].get<float>();
			}
			return true;
		}

		const json* field(const json& parent, const char* name)
		{
			if (!parent.is_object())
			{
				return nullptr;
			}

			const auto it = parent.find(name);
			return it == parent.end() ? nullptr : &*it;
		}

		bool readString(const json& parent, const char* name, String& out)
		{
			const json* node = field(parent, name);
			if (node == nullptr || !node->is_string())
			{
				return false;
			}

			out = node->get<String>();
			return true;
		}

		bool readFloat(const json& parent, const char* name, float& out)
		{
			const json* node = field(parent, name);
			if (node == nullptr || !node->is_number())
			{
				return false;
			}

			out = node->get<float>();
			return true;
		}

		bool readUInt(const json& parent, const char* name, uint32& out)
		{
			const json* node = field(parent, name);
			if (node == nullptr || !node->is_number_unsigned())
			{
				return false;
			}

			const uint64 value = node->get<uint64>();
			if (value > 0xFFFFFFFFull)
			{
				return false;
			}

			out = static_cast<uint32>(value);
			return true;
		}

		bool readBool(const json& parent, const char* name, bool& out)
		{
			const json* node = field(parent, name);
			if (node == nullptr || !node->is_boolean())
			{
				return false;
			}

			out = node->get<bool>();
			return true;
		}

		bool readVector(const json& parent, const char* name, Vector3& out)
		{
			const json* node = field(parent, name);
			float v[3];
			if (node == nullptr || !readNumbers(*node, 3, v))
			{
				return false;
			}

			out = Vector3(v[0], v[1], v[2]);
			return true;
		}

		bool readQuaternion(const json& parent, const char* name, Quaternion& out)
		{
			const json* node = field(parent, name);
			float v[4];
			if (node == nullptr || !readNumbers(*node, 4, v))
			{
				return false;
			}

			out = Quaternion(v[0], v[1], v[2], v[3]);
			return true;
		}

		bool readMatrix(const json& parent, const char* name, Matrix4& out)
		{
			const json* node = field(parent, name);
			float v[16];
			if (node == nullptr || !readNumbers(*node, 16, v))
			{
				return false;
			}

			out = Matrix4(v);
			return true;
		}

		const json* readArray(const json& parent, const char* name)
		{
			const json* node = field(parent, name);
			return (node != nullptr && node->is_array()) ? node : nullptr;
		}

		/// Reads an optional array of strings. A missing field leaves `out` empty; a field of another
		/// type, or holding anything but strings, fails.
		bool readOptionalStrings(const json& parent, const char* name, std::vector<String>& out)
		{
			out.clear();
			const json* node = field(parent, name);
			if (node == nullptr)
			{
				return true;
			}

			if (!node->is_array())
			{
				return false;
			}

			for (const json& entry : *node)
			{
				if (!entry.is_string())
				{
					return false;
				}
				out.push_back(entry.get<String>());
			}
			return true;
		}
	}

	std::vector<GlueCameraPose> SampleGlueCameraPoses(const GlueSceneDefinition& def, uint32 pathSamples)
	{
		std::vector<GlueCameraPose> poses;
		if (def.flightPath.empty())
		{
			return poses;
		}

		// Along the flight path
		for (uint32 i = 0; i < pathSamples; ++i)
		{
			const float t = pathSamples > 1 ? static_cast<float>(i) / static_cast<float>(pathSamples - 1) : 0.0f;
			const GlueCameraKey key = EvaluateGluePath(def.flightPath, t);
			poses.push_back({ key.position, key.lookAt });
		}

		// The gate pose, and the gate pose drifted sideways
		const GlueCameraKey gate = EvaluateGluePath(def.flightPath, 0.0f);
		poses.push_back({ gate.position, gate.lookAt });

		Vector3 forward = gate.lookAt - gate.position;
		forward.Normalize();
		const Vector3 drift = rightOf(forward) * GateDrift;
		poses.push_back({ gate.position + drift, gate.lookAt + drift });
		poses.push_back({ gate.position - drift, gate.lookAt - drift });

		// The plaza pose
		const GlueCameraKey& plazaKey = def.flightPath.back();
		const GlueCameraPose plaza{ plazaKey.position, plazaKey.lookAt };
		poses.push_back(plaza);

		// Push-in end poses towards every spot and the front spot
		for (const GlueSpot& spot : def.spots)
		{
			addPushIn(poses, plaza, spot.position);
		}
		addPushIn(poses, plaza, def.front.position);

		// A character walking from the entrance to the front spot, looking at the plaza
		for (uint32 i = 0; i < EntranceSamples; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(EntranceSamples - 1);
			Vector3 position = lerp(def.entrance, def.front.position, t);
			position.y += EyeHeight;
			poses.push_back({ position, plaza.lookAt });
		}

		return poses;
	}

	GlueBakeFrustumSet BuildGlueBakeFrusta(const std::vector<GlueCameraPose>& poses, float verticalFovDegrees, float farDistance)
	{
		GlueBakeFrustumSet result;
		result.verticalFovDegrees = verticalFovDegrees;

		const float tanV = std::tan(verticalFovDegrees * 0.5f * PiF / 180.0f);

		for (const GlueCameraPose& pose : poses)
		{
			Vector3 forward = pose.lookAt - pose.position;
			if (forward.Normalize() <= 0.0f)
			{
				forward = Vector3::NegativeUnitZ;
			}

			const Vector3 right = rightOf(forward);
			const Vector3 up = forward.Cross(right);

			for (const float aspect : AspectRatios)
			{
				const float tanH = tanV * aspect;

				GlueBakeFrustumSet::Frustum frustum;
				frustum.origin = pose.position;

				// Inward facing normals; a point p is inside when dot(n, p - origin) >= 0.
				Vector3 left = right + forward * tanH;
				Vector3 rightPlane = right * -1.0f + forward * tanH;
				Vector3 top = up * -1.0f + forward * tanV;
				Vector3 bottom = up + forward * tanV;
				left.Normalize();
				rightPlane.Normalize();
				top.Normalize();
				bottom.Normalize();

				frustum.planes[0] = Plane(forward, pose.position);
				frustum.planes[1] = Plane(forward * -1.0f, pose.position + forward * farDistance);
				frustum.planes[2] = Plane(left, pose.position);
				frustum.planes[3] = Plane(rightPlane, pose.position);
				frustum.planes[4] = Plane(top, pose.position);
				frustum.planes[5] = Plane(bottom, pose.position);

				result.frusta.push_back(frustum);
			}
		}

		return result;
	}

	bool GlueBakeFrustumSet::Intersects(const AABB& worldBounds, float margin) const
	{
		const Vector3 center = worldBounds.GetCenter();
		const Vector3 half = worldBounds.GetExtents() + Vector3(margin, margin, margin);

		for (const Frustum& frustum : frusta)
		{
			bool inside = true;
			for (const Plane& plane : frustum.planes)
			{
				if (plane.GetDistance(center) < -plane.normal.AbsDot(half))
				{
					inside = false;
					break;
				}
			}

			if (inside)
			{
				return true;
			}
		}

		return false;
	}

	float GlueBakeFrustumSet::MaxProjectedSizePixels(const AABB& worldBounds, float screenHeightPixels, float margin) const
	{
		const Vector3 size = worldBounds.GetSize();
		const float extent = std::max({ size.x, size.y, size.z });
		const float tanV = std::tan(verticalFovDegrees * 0.5f * PiF / 180.0f);
		const float pixelsPerMetreAtOne = screenHeightPixels / (2.0f * tanV);

		float best = 0.0f;
		for (const Frustum& frustum : frusta)
		{
			// Skip frusta the bounds, grown by margin, are outside of.
			const Vector3 center = worldBounds.GetCenter();
			const Vector3 half = worldBounds.GetExtents() + Vector3(margin, margin, margin);
			bool inside = true;
			for (const Plane& plane : frustum.planes)
			{
				if (plane.GetDistance(center) < -plane.normal.AbsDot(half))
				{
					inside = false;
					break;
				}
			}

			if (!inside)
			{
				continue;
			}

			// Distance to the nearest point of the bounds.
			const Vector3 nearest(
				std::clamp(frustum.origin.x, worldBounds.min.x, worldBounds.max.x),
				std::clamp(frustum.origin.y, worldBounds.min.y, worldBounds.max.y),
				std::clamp(frustum.origin.z, worldBounds.min.z, worldBounds.max.z));
			const float distance = std::max((nearest - frustum.origin).GetLength(), 0.01f);

			best = std::max(best, extent * pixelsPerMetreAtOne / distance);
		}

		return best;
	}

	bool IsGlueBakeInteriorGroup(uint32 groupFlags)
	{
		return (groupFlags & InteriorFlag) != 0 && (groupFlags & ExteriorFlag) == 0;
	}

	String WriteGlueBakeManifest(const GlueBakeManifest& manifest)
	{
		json root = json::object();
		root["version"] = manifest.version;
		root["sceneHash"] = manifest.sceneHash;

		json inputs = json::array();
		for (const GlueBakeInputFile& input : manifest.inputs)
		{
			inputs.push_back({ { "path", input.path }, { "hash", input.hash } });
		}
		root["inputs"] = std::move(inputs);

		json meshes = json::array();
		for (const GlueBakeMesh& mesh : manifest.meshes)
		{
			json node = { { "mesh", mesh.mesh }, { "transform", writeMatrix(mesh.transform) } };
			if (!mesh.materialOverrides.empty())
			{
				node["materialOverrides"] = mesh.materialOverrides;
			}
			meshes.push_back(std::move(node));
		}
		root["meshes"] = std::move(meshes);

		json tiles = json::array();
		for (const GlueBakeTerrainTile& tile : manifest.terrainTiles)
		{
			tiles.push_back({ { "mesh", tile.mesh }, { "material", tile.material }, { "splatting", tile.splatting } });
		}
		root["terrainTiles"] = std::move(tiles);

		json water = json::array();
		for (const GlueBakeMesh& mesh : manifest.water)
		{
			water.push_back({ { "mesh", mesh.mesh }, { "transform", writeMatrix(mesh.transform) } });
		}
		root["water"] = std::move(water);

		json foliage = json::array();
		for (const GlueBakeFoliage& item : manifest.foliage)
		{
			foliage.push_back({
				{ "mesh", item.mesh },
				{ "position", writeVector(item.position) },
				{ "rotation", writeQuaternion(item.rotation) },
				{ "scale", writeVector(item.scale) } });
		}
		root["foliage"] = std::move(foliage);

		json lights = json::array();
		for (const GlueBakeLight& light : manifest.lights)
		{
			lights.push_back({
				{ "type", light.type },
				{ "useAttenuation", light.useAttenuation },
				{ "color", light.color },
				{ "position", writeVector(light.position) },
				{ "intensity", light.intensity },
				{ "rotation", writeQuaternion(light.rotation) },
				{ "attenuationStart", light.attenuationStart },
				{ "attenuationEnd", light.attenuationEnd },
				{ "innerConeAngle", light.innerConeAngle },
				{ "outerConeAngle", light.outerConeAngle },
				{ "fogScattering", light.fogScattering } });
		}
		root["lights"] = std::move(lights);

		json sounds = json::array();
		for (const GlueBakeSound& sound : manifest.sounds)
		{
			sounds.push_back({
				{ "soundEntry", sound.soundEntry },
				{ "position", writeVector(sound.position) },
				{ "volume", sound.volume },
				{ "radius", sound.radius } });
		}
		root["sounds"] = std::move(sounds);

		json particles = json::array();
		for (const GlueBakeParticles& item : manifest.particles)
		{
			particles.push_back({ { "file", item.file }, { "transform", writeMatrix(item.transform) } });
		}
		root["particles"] = std::move(particles);

		root["bounds"] = { { "min", writeVector(manifest.bounds.min) }, { "max", writeVector(manifest.bounds.max) } };

		return root.dump();
	}

	std::optional<GlueBakeManifest> ParseGlueBakeManifest(const String& text)
	{
		const json root = json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			return std::nullopt;
		}

		GlueBakeManifest result;
		if (!readUInt(root, "version", result.version) || result.version != 1)
		{
			return std::nullopt;
		}

		if (!readString(root, "sceneHash", result.sceneHash))
		{
			return std::nullopt;
		}

		const json* inputs = readArray(root, "inputs");
		const json* meshes = readArray(root, "meshes");
		const json* tiles = readArray(root, "terrainTiles");
		const json* water = readArray(root, "water");
		const json* foliage = readArray(root, "foliage");
		const json* lights = readArray(root, "lights");
		const json* sounds = readArray(root, "sounds");
		const json* particles = readArray(root, "particles");
		const json* bounds = field(root, "bounds");
		if (!inputs || !meshes || !tiles || !water || !foliage || !lights || !sounds || !particles || !bounds)
		{
			return std::nullopt;
		}

		for (const json& node : *inputs)
		{
			GlueBakeInputFile item;
			if (!readString(node, "path", item.path) || !readString(node, "hash", item.hash))
			{
				return std::nullopt;
			}
			result.inputs.push_back(std::move(item));
		}

		for (const json& node : *meshes)
		{
			GlueBakeMesh item;
			if (!readString(node, "mesh", item.mesh) || !readMatrix(node, "transform", item.transform) ||
				!readOptionalStrings(node, "materialOverrides", item.materialOverrides))
			{
				return std::nullopt;
			}
			result.meshes.push_back(std::move(item));
		}

		for (const json& node : *tiles)
		{
			GlueBakeTerrainTile item;
			if (!readString(node, "mesh", item.mesh) || !readString(node, "material", item.material) || !readString(node, "splatting", item.splatting))
			{
				return std::nullopt;
			}
			result.terrainTiles.push_back(std::move(item));
		}

		for (const json& node : *water)
		{
			GlueBakeMesh item;
			if (!readString(node, "mesh", item.mesh) || !readMatrix(node, "transform", item.transform))
			{
				return std::nullopt;
			}
			result.water.push_back(std::move(item));
		}

		for (const json& node : *foliage)
		{
			GlueBakeFoliage item;
			if (!readString(node, "mesh", item.mesh)
				|| !readVector(node, "position", item.position)
				|| !readQuaternion(node, "rotation", item.rotation)
				|| !readVector(node, "scale", item.scale))
			{
				return std::nullopt;
			}
			result.foliage.push_back(std::move(item));
		}

		for (const json& node : *lights)
		{
			GlueBakeLight item;
			if (!readUInt(node, "type", item.type)
				|| !readBool(node, "useAttenuation", item.useAttenuation)
				|| !readUInt(node, "color", item.color)
				|| !readVector(node, "position", item.position)
				|| !readFloat(node, "intensity", item.intensity)
				|| !readQuaternion(node, "rotation", item.rotation)
				|| !readFloat(node, "attenuationStart", item.attenuationStart)
				|| !readFloat(node, "attenuationEnd", item.attenuationEnd)
				|| !readFloat(node, "innerConeAngle", item.innerConeAngle)
				|| !readFloat(node, "outerConeAngle", item.outerConeAngle)
				|| !readFloat(node, "fogScattering", item.fogScattering))
			{
				return std::nullopt;
			}
			result.lights.push_back(std::move(item));
		}

		for (const json& node : *sounds)
		{
			GlueBakeSound item;
			if (!readUInt(node, "soundEntry", item.soundEntry)
				|| !readVector(node, "position", item.position)
				|| !readFloat(node, "volume", item.volume)
				|| !readFloat(node, "radius", item.radius))
			{
				return std::nullopt;
			}
			result.sounds.push_back(std::move(item));
		}

		for (const json& node : *particles)
		{
			GlueBakeParticles item;
			if (!readString(node, "file", item.file) || !readMatrix(node, "transform", item.transform))
			{
				return std::nullopt;
			}
			result.particles.push_back(std::move(item));
		}

		if (!readVector(*bounds, "min", result.bounds.min) || !readVector(*bounds, "max", result.bounds.max))
		{
			return std::nullopt;
		}

		return result;
	}

	String HashGlueBakeBytes(const void* data, size_t size)
	{
		constexpr uint64 offsetBasis = 14695981039346656037ull;
		constexpr uint64 prime = 1099511628211ull;

		uint64 hash = offsetBasis;
		const uint8* bytes = static_cast<const uint8*>(data);
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= bytes[i];
			hash *= prime;
		}

		static constexpr char digits[] = "0123456789abcdef";
		String result(16, '0');
		for (int i = 15; i >= 0; --i)
		{
			result[static_cast<size_t>(i)] = digits[hash & 0xF];
			hash >>= 4;
		}
		return result;
	}

	std::vector<String> FindStaleGlueBakeInputs(const GlueBakeManifest& manifest, const std::function<std::optional<String>(const String& path)>& hashFile)
	{
		std::vector<String> stale;
		for (const GlueBakeInputFile& input : manifest.inputs)
		{
			const std::optional<String> current = hashFile(input.path);
			if (!current.has_value() || *current != input.hash)
			{
				stale.push_back(input.path);
			}
		}
		return stale;
	}

	bool ShouldUseGlueBake(const std::optional<GlueBakeManifest>& manifest, const String& currentSceneHash)
	{
		return manifest.has_value() && !currentSceneHash.empty() && manifest->sceneHash == currentSceneHash;
	}

	std::vector<GlueBakeMeshGroup> GroupGlueBakeMeshes(const std::vector<GlueBakeMesh>& meshes, const float cellSize)
	{
		using Key = std::tuple<String, std::vector<String>, bool, int32, int32>;
		std::map<Key, std::vector<size_t>> buckets;

		for (size_t i = 0; i < meshes.size(); ++i)
		{
			const GlueBakeMesh& placement = meshes[i];

			int32 cellX = 0;
			int32 cellZ = 0;
			if (cellSize > 0.0f)
			{
				const Vector3 position = placement.transform.GetTrans();
				cellX = static_cast<int32>(std::floor(position.x / cellSize));
				cellZ = static_cast<int32>(std::floor(position.z / cellSize));
			}

			buckets[Key(placement.mesh, placement.materialOverrides, placement.transform.HasNegativeScale(), cellX, cellZ)].push_back(i);
		}

		std::vector<GlueBakeMeshGroup> groups;
		groups.reserve(buckets.size());
		for (auto& [key, placements] : buckets)
		{
			GlueBakeMeshGroup group;
			group.mesh = std::get<0>(key);
			group.materialOverrides = std::get<1>(key);
			group.mirrored = std::get<2>(key);
			group.cellX = std::get<3>(key);
			group.cellZ = std::get<4>(key);
			group.placements = std::move(placements);
			groups.push_back(std::move(group));
		}

		return groups;
	}
}
