// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_scene_collector.h"

#include "editors/world_editor/world_editor_instance.h"

#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/world_model_flatten.h"
#include "scene_graph/world_model_instance.h"
#include "terrain/constants.h"
#include "terrain/page.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <set>

namespace mmo
{
	namespace
	{
		/// Grows every bounds before the frustum test, so an object just outside a frustum edge is kept.
		constexpr float VisibilityMargin = 1.0f;

		/// Screen height the small-object threshold is measured at.
		constexpr float ReferenceScreenHeight = 1080.0f;

		/// Height range a terrain page is assumed to span before its heights are known.
		constexpr float PageCandidateMinY = -2000.0f;
		constexpr float PageCandidateMaxY = 4000.0f;

		/// Audible range of a sound emitter that uses its sound entry's range (maxDistance 0), which
		/// the editor does not resolve here.
		constexpr float DefaultSoundRange = 100.0f;

		/// Assumed extent of a particle system around its origin for the visibility test.
		constexpr float ParticleRadius = 10.0f;

		/// Hash recorded for an input that could not be read.
		const char* const MissingInputHash = "missing";

		bool IsFinite(const float value)
		{
			return std::isfinite(value);
		}

		bool IsFinite(const Vector3& v)
		{
			return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
		}

		bool IsFinite(const Quaternion& q)
		{
			return IsFinite(q.x) && IsFinite(q.y) && IsFinite(q.z) && IsFinite(q.w);
		}

		bool IsFinite(const Matrix4& m)
		{
			return m.IsFinite();
		}

		AABB SphereBounds(const Vector3& center, const float radius)
		{
			return AABB(center - Vector3(radius, radius, radius), center + Vector3(radius, radius, radius));
		}

		bool EndsWith(const String& value, const char* suffix)
		{
			const size_t length = std::char_traits<char>::length(suffix);
			return value.size() >= length && value.compare(value.size() - length, length, suffix) == 0;
		}

		/// Records the files a collection reads and hashes them once each.
		class InputRecorder final
		{
		public:
			void Add(const String& path)
			{
				if (path.empty() || m_files.contains(path))
				{
					return;
				}

				std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(path);
				if (!file)
				{
					// Kept with a hash no file has, so the staleness check reports it.
					WLOG("Glue bake: cannot read input " << path << " to fingerprint it, recorded as missing");
					m_files.emplace(path, MissingInputHash);
					return;
				}

				const std::vector<char> bytes{ std::istreambuf_iterator<char>(*file), std::istreambuf_iterator<char>() };
				m_files.emplace(path, HashGlueBakeBytes(bytes.data(), bytes.size()));
			}

			std::vector<GlueBakeInputFile> Finish() const
			{
				std::vector<GlueBakeInputFile> inputs;
				inputs.reserve(m_files.size());
				for (const auto& [path, hash] : m_files)
				{
					inputs.push_back(GlueBakeInputFile{ path, hash });
				}

				return inputs;
			}

		private:
			std::map<String, String> m_files;
		};

		AABB PageBoundsXZ(const PagePosition& page, const float minY, const float maxY)
		{
			const float minX = static_cast<float>((static_cast<double>(page.x()) - 32.0) * terrain::constants::PageSize);
			const float minZ = static_cast<float>((static_cast<double>(page.y()) - 32.0) * terrain::constants::PageSize);
			const float size = static_cast<float>(terrain::constants::PageSize);
			return AABB(Vector3(minX, minY, minZ), Vector3(minX + size, maxY, minZ + size));
		}

		/// Expands a mesh's material overrides to one entry per submesh (see FlattenedMeshRef).
		std::vector<String> ExpandMaterialOverrides(const std::vector<String>& overrides, const uint16 subMeshCount)
		{
			if (overrides.empty())
			{
				return {};
			}

			if (overrides.size() == 1)
			{
				return std::vector<String>(subMeshCount, overrides.front());
			}

			std::vector<String> expanded = overrides;
			expanded.resize(subMeshCount);
			return expanded;
		}
	}

	GlueCollection CollectGlueScene(WorldEditorInstance& world, const GlueSceneDefinition& def, const GlueBakeFrustumSet& frusta, const float smallObjectPixels)
	{
		GlueCollection collection;

		const terrain::Terrain* terrain = world.GetTerrain();
		if (!terrain || !world.HasTerrain())
		{
			ELOG("Glue bake: the world has no terrain");
			return collection;
		}

		if (frusta.frusta.empty())
		{
			ELOG("Glue bake: no camera frusta to collect for");
			return collection;
		}

		String worldPath = world.GetWorldPath().string();
		std::replace(worldPath.begin(), worldPath.end(), '\\', '/');
		if (worldPath.find(def.world) == String::npos)
		{
			WLOG("Glue bake: the open world " << worldPath << " is not the glue scene's world " << def.world);
		}

		InputRecorder inputs;

		// Terrain pages. A cheap test with a generous height range first, then the real heights.
		std::set<std::pair<uint32, uint32>> candidatePages;
		for (uint32 z = 0; z < terrain->GetHeight(); ++z)
		{
			for (uint32 x = 0; x < terrain->GetWidth(); ++x)
			{
				if (!frusta.Intersects(PageBoundsXZ(PagePosition(x, z), PageCandidateMinY, PageCandidateMaxY), VisibilityMargin))
				{
					continue;
				}

				candidatePages.emplace(x, z);

				const terrain::Page* page = world.GetPreparedTerrainPage(PagePosition(x, z));
				if (!page)
				{
					continue;
				}

				float minY = std::numeric_limits<float>::max();
				float maxY = std::numeric_limits<float>::lowest();
				for (size_t j = 0; j < terrain::constants::OuterVerticesPerPageSide; ++j)
				{
					for (size_t i = 0; i < terrain::constants::OuterVerticesPerPageSide; ++i)
					{
						const float height = page->GetHeightAt(i, j);
						minY = std::min(minY, height);
						maxY = std::max(maxY, height);
					}
				}

				if (minY > maxY || !frusta.Intersects(PageBoundsXZ(PagePosition(x, z), minY, maxY), VisibilityMargin))
				{
					continue;
				}

				collection.terrainPages.emplace_back(x, z);
			}
		}

		// Entities: a world model is stored with the page its origin lies in but reaches into the
		// neighbours, so the pages around every candidate are scanned too.
		std::set<std::pair<uint32, uint32>> entityPageSet;
		for (const auto& [x, z] : candidatePages)
		{
			for (int32 dz = -1; dz <= 1; ++dz)
			{
				for (int32 dx = -1; dx <= 1; ++dx)
				{
					const int32 px = static_cast<int32>(x) + dx;
					const int32 pz = static_cast<int32>(z) + dz;
					if (px >= 0 && pz >= 0 && px < static_cast<int32>(terrain->GetWidth()) && pz < static_cast<int32>(terrain->GetHeight()))
					{
						entityPageSet.emplace(static_cast<uint32>(px), static_cast<uint32>(pz));
					}
				}
			}
		}

		for (const auto& [x, z] : entityPageSet)
		{
			collection.entityPages.emplace_back(x, z);
		}

		for (const PagePosition& page : collection.entityPages)
		{
			const bool isCandidatePage = candidatePages.contains({ static_cast<uint32>(page.x()), static_cast<uint32>(page.y()) });
			for (const String& file : world.GetPageInputFiles(page))
			{
				// The terrain of every candidate page is read (its heights decide visibility); the
				// pages around the candidates only count for their placements.
				if (!isCandidatePage && EndsWith(file, ".tile"))
				{
					continue;
				}

				inputs.Add(file);
			}
		}

		world.ForceLoadPages(collection.entityPages);

		// Adds a mesh placement if a frustum sees it and it is large enough on screen.
		const auto addMesh = [&](const String& meshPath, const Matrix4& transform, const std::vector<String>& materialOverrides) -> bool
		{
			// Read whatever the outcome, so it is an input even when culled
			inputs.Add(meshPath);

			if (!IsFinite(transform))
			{
				WLOG("Glue bake: mesh " << meshPath << " has a non-finite transform, skipped");
				return false;
			}

			const MeshPtr mesh = MeshManager::Get().Load(meshPath);
			if (!mesh)
			{
				++collection.missingMeshes;
				return false;
			}

			AABB bounds = mesh->GetBounds();
			bounds.Transform(transform);

			if (!frusta.Intersects(bounds, VisibilityMargin))
			{
				++collection.skippedOutside;
				return false;
			}

			if (frusta.MaxProjectedSizePixels(bounds, ReferenceScreenHeight, VisibilityMargin) < smallObjectPixels)
			{
				++collection.skippedSmall;
				return false;
			}

			GlueRenderItem item;
			item.mesh = mesh;
			item.meshPath = meshPath;
			item.world = transform;
			item.materialOverrides = ExpandMaterialOverrides(materialOverrides, mesh->GetSubMeshCount());
			item.mergeable = WorldModelInstance::CanMergeMesh(*mesh);
			item.worldBounds = bounds;

			if (!item.mergeable)
			{
				++collection.unmergeable;
			}

			collection.items.push_back(std::move(item));
			return true;
		};

		// The distance from the nearest camera to a point.
		const auto nearestCameraDistance = [&frusta](const Vector3& point)
		{
			float nearest = std::numeric_limits<float>::max();
			for (const auto& frustum : frusta.frusta)
			{
				nearest = std::min(nearest, (frustum.origin - point).GetLength());
			}
			return nearest;
		};

		const auto addSound = [&](const PlacedSoundEmitter& emitter)
		{
			if (!IsFinite(emitter.position) || !IsFinite(emitter.volume) || !IsFinite(emitter.maxDistance))
			{
				return;
			}

			const float range = emitter.maxDistance > 0.0f ? emitter.maxDistance : DefaultSoundRange;
			if (nearestCameraDistance(emitter.position) > range)
			{
				++collection.inaudibleSounds;
				return;
			}

			collection.sounds.push_back(GlueBakeSound{ emitter.soundEntryId, emitter.position, emitter.volume, emitter.maxDistance });
		};

		const auto addLight = [&](const WorldModelLight& light)
		{
			if (!IsFinite(light.position) || !IsFinite(light.rotation) || !IsFinite(light.intensity) ||
				!IsFinite(light.attenuationStart) || !IsFinite(light.attenuationEnd) ||
				!IsFinite(light.innerConeAngle) || !IsFinite(light.outerConeAngle) || !IsFinite(light.fogScattering))
			{
				WLOG("Glue bake: skipped a light with non-finite parameters");
				return;
			}

			// Ambient lights become no scene light at runtime (WorldModelInstance::CreateLightFrom).
			if (light.type == WorldModelLight::LightType::Ambient)
			{
				return;
			}

			// Directional lights reach everywhere; a ranged light only where its range does.
			const bool ranged = light.type == WorldModelLight::LightType::Omni || light.type == WorldModelLight::LightType::Spot;
			if (ranged && light.attenuationEnd > 0.0f && !frusta.Intersects(SphereBounds(light.position, light.attenuationEnd), VisibilityMargin))
			{
				return;
			}

			GlueBakeLight baked;
			baked.type = static_cast<uint32>(light.type);
			baked.useAttenuation = light.useAttenuation;
			baked.color = light.color;
			baked.position = light.position;
			baked.intensity = light.intensity;
			baked.rotation = light.rotation;
			baked.attenuationStart = light.attenuationStart;
			baked.attenuationEnd = light.attenuationEnd;
			baked.innerConeAngle = light.innerConeAngle;
			baked.outerConeAngle = light.outerConeAngle;
			baked.fogScattering = light.fogScattering;
			collection.lights.push_back(baked);
		};

		const auto addParticles = [&](const FlattenedParticles& particles)
		{
			inputs.Add(particles.path);

			if (!IsFinite(particles.world))
			{
				return;
			}

			if (!frusta.Intersects(SphereBounds(particles.world.GetTrans(), ParticleRadius), VisibilityMargin))
			{
				return;
			}

			collection.particles.push_back(GlueBakeParticles{ particles.path, particles.world });
		};

		// Placements
		uint32 worldModelPlacements = 0;
		uint32 meshPlacements = 0;
		world.ForEachLoadedPlacement([&](const GlueBakePlacement& placement)
		{
			// Read whatever the outcome, so it is an input even when culled
			inputs.Add(placement.assetPath);

			const FlattenTransform placementTransform{ placement.position, placement.rotation, placement.scale };
			const Matrix4 transform = placementTransform.ToMatrix();

			if (!placement.isWorldModel)
			{
				++meshPlacements;

				addMesh(placement.assetPath, transform, placement.materialOverrides);

				// Audible even when out of view
				if (const MeshPtr& mesh = placement.mesh)
				{
					std::vector<PlacedSoundEmitter> emitters;
					AppendPlacedSoundEmitters(*mesh, placement.position, placement.rotation, placement.scale, emitters);
					for (const PlacedSoundEmitter& emitter : emitters)
					{
						addSound(emitter);
					}
				}
				return;
			}

			++worldModelPlacements;

			FlattenOptions options;
			options.doodadSet = placement.doodadSet;
			options.propKitPaths = placement.kitPaths;
			options.maxChildDepth = WorldModelInstance::MaxChildDepth;

			// Houses keep their outer walls in interior-flagged storey groups, which the live client draws
			// from outside; their furniture, lights and nested rooms stay out of the bake.
			options.includeInteriorGroupMeshes = true;

			FlattenReport report;
			const std::vector<FlattenedMeshRef> refs = FlattenWorldModelExterior(placement.worldModel, placementTransform, options, &report);

			collection.skippedInterior += report.skippedInteriorGroups;
			collection.includedInterior += report.includedInteriorGroups;
			collection.skippedInteriorMeshes += report.skippedInteriorMeshes;
			collection.skippedInteriorChildren += report.skippedInteriorChildren;

			size_t kept = 0;
			for (const FlattenedMeshRef& ref : refs)
			{
				if (addMesh(ref.meshPath, ref.world, ref.materialOverrides))
				{
					++kept;
				}
			}

			const FlattenedWorldModelExtras extras = FlattenWorldModelExtras(placement.worldModel, placementTransform, options);
			for (const WorldModelLight& light : extras.lights)
			{
				addLight(light);
			}
			for (const PlacedSoundEmitter& emitter : extras.sounds)
			{
				addSound(emitter);
			}
			for (const FlattenedParticles& particles : extras.particles)
			{
				addParticles(particles);
			}

			// Every file the walks read, whether it contributes or not
			for (const String& path : report.worldModelPaths)
			{
				inputs.Add(path);
			}
			for (const String& path : report.propKitPaths)
			{
				inputs.Add(path);
			}
			for (const String& path : extras.meshPaths)
			{
				inputs.Add(path);
			}

			if (kept > 0)
			{
				ILOG("Glue bake: world model " << placement.assetPath << ": " << kept << " of " << refs.size()
					<< " meshes visible, " << report.includedInteriorGroups << " interior groups kept as facades, "
					<< report.skippedInteriorGroups << " skipped");
			}
		});

		// Authored foliage
		world.ForEachFoliageInstance(collection.entityPages, [&](const InstancedFoliageInstance& instance)
		{
			if (!IsFinite(instance.position) || !IsFinite(instance.rotation) || !IsFinite(instance.scale))
			{
				return;
			}

			// Read whatever the outcome, so it is an input even when culled
			inputs.Add(instance.meshName);

			const MeshPtr mesh = MeshManager::Get().Load(instance.meshName);
			if (!mesh)
			{
				++collection.missingMeshes;
				return;
			}

			Matrix4 transform;
			transform.MakeTransform(instance.position, instance.scale, instance.rotation);
			AABB bounds = mesh->GetBounds();
			bounds.Transform(transform);

			if (!frusta.Intersects(bounds, VisibilityMargin))
			{
				++collection.skippedOutside;
				return;
			}

			if (frusta.MaxProjectedSizePixels(bounds, ReferenceScreenHeight, VisibilityMargin) < smallObjectPixels)
			{
				++collection.skippedSmall;
				return;
			}

			collection.foliage.push_back(GlueBakeFoliage{ instance.meshName, instance.position, instance.rotation, instance.scale });
		});

		world.ReleaseForcedPages();

		collection.inputs = inputs.Finish();

		ILOG("Glue bake: scanned " << worldModelPlacements << " world model and " << meshPlacements << " mesh placements on "
			<< collection.entityPages.size() << " pages");

		return collection;
	}
}
