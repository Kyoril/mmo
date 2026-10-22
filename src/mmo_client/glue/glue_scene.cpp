// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_scene.h"
#include "plaza_avatars.h"

#include "environment_apply.h"
#include "world_deserializer.h"

#include "assets/asset_registry.h"
#include "base/clock.h"
#include "base/profiler.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "console/console_var.h"
#include "graphics/graphics_device.h"
#include "graphics/material_instance.h"
#include "graphics/sky_component.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"
#include "math/constants.h"
#include "math/ray.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "scene_graph/instanced_foliage.h"
#include "scene_graph/light.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/particle_emitter_serializer.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/sub_entity.h"
#include "scene_graph/world_model_batch.h"
#include "scene_graph/world_model_instance.h"
#include "terrain/constants.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <set>

namespace mmo
{
	namespace
	{
		/// Milliseconds of queued streaming work finished per frame.
		constexpr double dispatchBudgetMs = 4.0;

		/// Milliseconds of queued streaming work finished per frame while a fade covers the scene
		/// (before the gate view is ready, or while an arrival waits for the plaza). Nothing moving
		/// is visible then, and the world models (hundreds of steps for the town) arrive sooner.
		constexpr double coveredDispatchBudgetMs = 25.0;

		/// Half the width of the idle drift at the gate, in metres.
		constexpr float driftAmplitude = 0.3f;

		/// Seconds of one idle drift cycle at the gate.
		constexpr float driftPeriodSeconds = 20.0f;

		/// Share of the way from the plaza camera to the selected character covered by the push-in.
		constexpr float pushInFraction = 0.4f;

		/// Farthest a cursor ray reaches, in metres.
		constexpr float pickDistance = 200.0f;

		/// Height above the character's feet the push-in moves towards, in metres (about chest height).
		constexpr float pushInTargetHeight = 1.4f;

		/// Milliseconds after loading of the gate (resp. the plaza) began when it counts as ready
		/// even if it is not loaded, so neither the fade nor the flight input lock lasts forever.
		constexpr GameTime readinessTimeoutMs = 8000;

		/// Overrides the camera path parameter while >= 0, to inspect points of the flight path.
		ConsoleVar* s_glueCameraPathVar = nullptr;

		/// The manifest mmo_edit --bake-glue-scene writes.
		constexpr const char* glueBakeManifestFile = "Interface/GlueUI/GlueBake/GlueBake.json";

		/// Milliseconds of bake creation per frame once the gate view shows. While the fade still
		/// covers the gate, all remaining steps run in one frame instead: splitting them only adds
		/// frames to the load time.
		constexpr double bakeStepBudgetMs = 150.0;

		/// Edge length of the cells baked placements are batched in, in metres: the cell size of
		/// the world's instanced trees, so batches stay frustum culled in parts.
		constexpr float bakeBatchCellSize = static_cast<float>(terrain::constants::PageSize / 4.0);

		/// Triangles of every submesh of a mesh.
		uint64 CountTriangles(const Mesh& mesh)
		{
			uint64 triangles = 0;
			for (uint16 i = 0; i < mesh.GetSubMeshCount(); ++i)
			{
				const SubMesh& subMesh = mesh.GetSubMesh(i);
				if (subMesh.indexData)
				{
					triangles += subMesh.indexData->indexCount / 3;
				}
			}

			return triangles;
		}
	}

	GlueScene::GlueScene(const proto_client::Project& project, GlueSceneDefinition definition, const String& definitionHash, SoundEntryPlayer& soundEntryPlayer, IAudio& audio)
		: m_project(project)
		, m_definition(std::move(definition))
		, m_director(m_definition.flightSeconds, m_definition.flyOutSeconds)
		, m_environmentProfiles(project.environmentProfiles)
		, m_ambientSounds(soundEntryPlayer, audio)
	{
#ifdef MMO_WITH_DEV_COMMANDS
		if (s_glueCameraPathVar == nullptr)
		{
			s_glueCameraPathVar = ConsoleVarMgr::RegisterConsoleVar("glueCameraPath", "Pins the glue scene camera to this point of the flight path (0 = gate, 1 = plaza). Negative to follow the director (developer builds only).", "-1");
		}
#endif

		m_scene = std::make_unique<OctreeScene>();
		m_scene->SetFogEnabled(true);

		m_sky = std::make_unique<SkyComponent>(*m_scene, nullptr);
		m_environment.SetTarget(m_environmentProfiles.Get(m_definition.environmentProfile), true);

		m_camera = m_scene->CreateCamera("Default");
		m_cameraNode = &m_scene->CreateSceneNode("GlueCamera");

		// Without a fixed yaw axis, LookAt turns the current orientation by the shortest arc every
		// frame, so the camera picks up roll that never goes away (a tilted horizon).
		m_cameraNode->SetFixedYawAxis(true, Vector3::UnitY);
		m_scene->GetRootSceneNode().AddChild(*m_cameraNode);
		m_cameraNode->AttachObject(*m_camera);

		m_worldRootNode = m_scene->GetRootSceneNode().CreateChildSceneNode();

		m_plazaAvatars = std::make_unique<PlazaAvatars>(*m_scene, m_project, m_definition);

		// Water pages resolve their surface material through this static hook. WorldState sets the
		// same resolver, but the glue scene is shown before any world was ever entered.
		terrain::Page::SetWaterMaterialResolver([&project = m_project](const terrain::WaterType waterType) -> String
			{
				const auto* profile = project.waterProfiles.getById(static_cast<uint32>(waterType));
				if (profile == nullptr || !profile->has_surface_material())
				{
					return String();
				}

				return profile->surface_material();
			});

		const auto readStart = std::chrono::steady_clock::now();
		if (std::optional<GlueBakeManifest> bake = ReadBake(definitionHash))
		{
			m_bakeReadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readStart).count();

			// Everything the views show is listed in the bake: no world instance, no pages, no
			// loader thread.
			m_useBake = true;
			StartBakePrefetch(*bake);
			QueueBakeSteps(std::make_shared<const GlueBakeManifest>(std::move(*bake)));
			UpdateCamera(0.0f);
			return;
		}

		m_work = std::make_unique<asio::io_service::work>(m_workQueue);
		m_dispatcherWork = std::make_unique<asio::io_service::work>(m_dispatcher);
		auto& workQueue = m_workQueue;
		m_loaderThread = std::thread([&workQueue]()
			{
				workQueue.run();
			});

		m_pageQueue = m_definition.gatePages;
		m_pageQueue.insert(m_pageQueue.end(), m_definition.plazaPages.begin(), m_definition.plazaPages.end());

		if (!LoadWorld())
		{
			// Nothing will ever load: an empty queue reports both views ready, so the screens are not
			// left behind a fade forever.
			m_pageQueue.clear();
		}

		UpdateCamera(0.0f);
	}

	GlueScene::~GlueScene()
	{
		// Characters and their item meshes go before anything else touches the scene.
		m_plazaAvatars.reset();

		m_stopBakePrefetch = true;
		if (m_bakePrefetchThread.joinable())
		{
			m_bakePrefetchThread.join();
		}

		m_work.reset();
		m_dispatcherWork.reset();
		m_workQueue.stop();
		m_dispatcher.stop();
		if (m_loaderThread.joinable())
		{
			m_loaderThread.join();
		}

		// The world instance never clears its ambient sound target itself.
		m_ambientSounds.Clear();
		m_worldInstance.reset();

		// The bake's batches are owned here, not by the scene: detach them before the scene goes.
		for (const BakeBatch& entry : m_bakeBatches)
		{
			entry.node->DetachObject(*entry.batch);
			m_scene->DestroySceneNode(*entry.node);
		}
		m_bakeBatches.clear();
		m_bakeFoliage.reset();
		m_bakeSteps.clear();

		m_sky.reset();
		m_scene->Clear();
	}

	Scene& GlueScene::GetScene()
	{
		return *m_scene;
	}

	Vector3 GlueScene::GetCameraPosition() const
	{
		return m_camera->GetDerivedPosition();
	}

	void GlueScene::SetTimeOfDay(const GameTime msSinceMidnight)
	{
		const float normalized = static_cast<float>(static_cast<double>(msSinceMidnight % constants::OneDay) / static_cast<double>(constants::OneDay));
		m_sky->SetNormalizedTimeOfDay(normalized);
	}

	bool GlueScene::ConsumeFlightFinished()
	{
		const bool finished = m_flightFinished;
		m_flightFinished = false;
		return finished;
	}

	void GlueScene::SnapToPlazaWhenReady()
	{
		m_pendingArrival = PendingArrival::Snap;
	}

	void GlueScene::FlyInWhenReady()
	{
		m_pendingArrival = PendingArrival::FlyIn;
	}

	bool GlueScene::ConsumeDeferredFlightStarted()
	{
		const bool started = m_deferredFlightStarted;
		m_deferredFlightStarted = false;
		return started;
	}

	void GlueScene::SkipFlight()
	{
		if (m_pendingArrival == PendingArrival::FlyIn)
		{
			m_pendingArrival = PendingArrival::Snap;
		}

		m_director.Skip();
	}

	bool GlueScene::IsFlightActive() const
	{
		const GlueCameraMode mode = m_director.GetMode();
		return m_pendingArrival != PendingArrival::None || mode == GlueCameraMode::FlyingIn || mode == GlueCameraMode::FlyingOut;
	}

	void GlueScene::SnapToGate()
	{
		m_director.SnapToGate();

		// A flight that ended (or was skipped) since the last Update must not be reported after
		// the jump: drop both the director's flag and the latch.
		(void)m_director.ConsumeFlightFinished();
		m_flightFinished = false;

		m_pendingArrival = PendingArrival::None;
		m_deferredFlightStarted = false;
		m_pushInSeconds = 0.0f;
		m_pushInElapsed = 0.0f;
		m_pushInFinished = false;
	}

	void GlueScene::PushIn(const float seconds, const Vector3& characterPosition)
	{
		m_pushInCharacterPosition = characterPosition;

		if (seconds <= 0.0f)
		{
			// Finished at once: hold the end of the push-in.
			m_pushInSeconds = 1.0f;
			m_pushInElapsed = 1.0f;
			m_pushInFinished = true;
			return;
		}

		m_pushInSeconds = seconds;
		m_pushInElapsed = 0.0f;
		m_pushInFinished = false;
	}

	bool GlueScene::ConsumePushInFinished()
	{
		const bool finished = m_pushInFinished;
		m_pushInFinished = false;
		return finished;
	}

	bool GlueScene::ProjectToScreen(const Vector3& worldPosition, float& x, float& y) const
	{
		x = 0.0f;
		y = 0.0f;

		const Vector3 cameraToPosition = worldPosition - m_camera->GetDerivedPosition();
		if (cameraToPosition.Dot(m_camera->GetDerivedDirection()) <= 0.0f)
		{
			return false;
		}

		m_camera->GetNormalizedScreenPosition(worldPosition, x, y);
		return x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f;
	}

	int32 GlueScene::PickPlazaCharacter(const float x, const float y) const
	{
		if (IsFlightActive() || x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f)
		{
			return -1;
		}

		return m_plazaAvatars->Pick(m_camera->GetCameraToViewportRay(x, y, pickDistance));
	}

	std::optional<GlueBakeManifest> GlueScene::ReadBake(const String& definitionHash)
	{
		const std::unique_ptr<std::istream> stream = AssetRegistry::OpenFile(glueBakeManifestFile);
		if (!stream)
		{
			WLOG("Glue scene: no glue bake (" << glueBakeManifestFile << " is missing), streaming the world instead");
			return std::nullopt;
		}

		const String text{ std::istreambuf_iterator<char>(*stream), std::istreambuf_iterator<char>() };
		std::optional<GlueBakeManifest> manifest = ParseGlueBakeManifest(text);
		if (!manifest)
		{
			WLOG("Glue scene: " << glueBakeManifestFile << " is unparsable, streaming the world instead");
			return std::nullopt;
		}

		if (!ShouldUseGlueBake(manifest, definitionHash))
		{
			WLOG("Glue scene: the glue bake was made from another GlueScene.json (scene hash " << manifest->sceneHash
				<< ", current " << definitionHash << "), streaming the world instead. Rebake with mmo_edit --bake-glue-scene.");
			return std::nullopt;
		}

		return manifest;
	}

	void GlueScene::StartBakePrefetch(const GlueBakeManifest& manifest)
	{
		// In the order the steps load them, each file once.
		std::vector<String> files;
		std::set<String> seen;
		const auto add = [&files, &seen](const String& file)
		{
			if (!file.empty() && seen.insert(file).second)
			{
				files.push_back(file);
			}
		};

		for (const GlueBakeTerrainTile& tile : manifest.terrainTiles)
		{
			add(tile.mesh);
			add(tile.material);
			add(tile.splatting);
		}
		for (const GlueBakeMesh& water : manifest.water)
		{
			add(water.mesh);
		}
		for (const GlueBakeMesh& mesh : manifest.meshes)
		{
			add(mesh.mesh);
			for (const String& material : mesh.materialOverrides)
			{
				add(material);
			}
		}
		for (const GlueBakeFoliage& foliage : manifest.foliage)
		{
			add(foliage.mesh);
		}
		for (const GlueBakeParticles& particles : manifest.particles)
		{
			add(particles.file);
		}

		// Prefetch is what streaming threads use: it only reads files (and what they reference)
		// into memory, and skips files the main thread opened already.
		std::atomic<bool>& stop = m_stopBakePrefetch;
		m_bakePrefetchThread = std::thread([files = std::move(files), &stop]()
			{
				for (const String& file : files)
				{
					if (stop)
					{
						return;
					}

					AssetRegistry::Prefetch(file);
				}
			});
	}

	void GlueScene::QueueBakeSteps(std::shared_ptr<const GlueBakeManifest> manifest)
	{
		// Terrain and water first, then the placed meshes, then everything that only decorates.
		for (size_t i = 0; i < manifest->terrainTiles.size(); ++i)
		{
			m_bakeSteps.push_back(BakeStep{ BakePhaseTerrain, [this, manifest, i]()
				{
					CreateBakeTerrainTile(manifest->terrainTiles[i]);
				} });
		}

		for (size_t i = 0; i < manifest->water.size(); ++i)
		{
			m_bakeSteps.push_back(BakeStep{ BakePhaseWater, [this, manifest, i]()
				{
					const GlueBakeMesh& water = manifest->water[i];
					if (Entity* entity = CreateBakeEntity(water.mesh, water.transform, water.materialOverrides))
					{
						// Like the live water surface.
						entity->SetCastShadows(false);
					}
				} });
		}

		auto groups = std::make_shared<const std::vector<GlueBakeMeshGroup>>(GroupGlueBakeMeshes(manifest->meshes, bakeBatchCellSize));
		for (size_t i = 0; i < groups->size(); ++i)
		{
			m_bakeSteps.push_back(BakeStep{ BakePhaseMeshes, [this, manifest, groups, i]()
				{
					CreateBakeMeshGroup(manifest->meshes, (*groups)[i]);
				} });
		}

		m_bakeSteps.push_back(BakeStep{ BakePhaseDecoration, [this, manifest]()
			{
				CreateBakeFoliage(manifest->foliage);

				for (const GlueBakeLight& light : manifest->lights)
				{
					CreateBakeLight(light);
				}

				if (!manifest->sounds.empty())
				{
					std::vector<AmbientSoundSource> sources;
					sources.reserve(manifest->sounds.size());
					for (const GlueBakeSound& sound : manifest->sounds)
					{
						sources.push_back(AmbientSoundSource{ sound.soundEntry, sound.position, sound.volume, sound.radius });
					}

					m_ambientSounds.Add(ambient_sound_owner::MapEntity, 0, std::move(sources));
				}

				for (const GlueBakeParticles& particles : manifest->particles)
				{
					CreateBakeParticles(particles);
				}
			} });
	}

	void GlueScene::UpdateBakeLoading()
	{
		ApplyReadinessTimeout();

		if (m_nextBakeStep >= m_bakeSteps.size())
		{
			return;
		}

		PROFILE_SCOPE("GlueScene::BakeLoading");

		++m_bakeFrames;

		// Behind the opaque fade nothing is shown, so the whole bake is created at once. Only steps
		// left after the fade lifted (a readiness timeout) are spread over frames.
		const bool covered = !m_gateReady;
		const auto start = std::chrono::steady_clock::now();
		auto stepStart = start;
		while (m_nextBakeStep < m_bakeSteps.size())
		{
			const BakeStep& step = m_bakeSteps[m_nextBakeStep++];
			step.run();

			const auto now = std::chrono::steady_clock::now();
			m_bakePhaseMs[step.phase] += std::chrono::duration<double, std::milli>(now - stepStart).count();
			stepStart = now;

			if (!covered && std::chrono::duration<double, std::milli>(now - start).count() >= bakeStepBudgetMs)
			{
				break;
			}
		}

		if (m_nextBakeStep < m_bakeSteps.size())
		{
			return;
		}

		ILOG("Glue scene: glue bake loaded in " << (GetAsyncTimeMs() - m_gateLoadStartMs) << " ms (meshes "
			<< (m_bakeEntityCount + m_bakeInstanceCount) << ", tris " << m_bakeTriangleCount << "; "
			<< m_bakeBatches.size() << " instanced batches of " << m_bakeInstanceCount << " placements, "
			<< m_bakeEntityCount << " entities, " << (m_bakeFoliage ? m_bakeFoliage->GetInstanceCount() : 0) << " foliage)");
		ILOG("Glue scene: glue bake split: manifest " << static_cast<int32>(m_bakeReadMs) << " ms, terrain " << static_cast<int32>(m_bakePhaseMs[BakePhaseTerrain])
			<< " ms, water " << static_cast<int32>(m_bakePhaseMs[BakePhaseWater]) << " ms, meshes " << static_cast<int32>(m_bakePhaseMs[BakePhaseMeshes])
			<< " ms, foliage/lights/sounds/particles " << static_cast<int32>(m_bakePhaseMs[BakePhaseDecoration]) << " ms, over " << m_bakeFrames << " frames");

		// The bake holds both views: the plaza is ready with the gate.
		if (!m_gateReady)
		{
			MarkGateReady(false);
		}
		m_plazaReady = true;
	}

	void GlueScene::CreateBakeMeshGroup(const std::vector<GlueBakeMesh>& meshes, const GlueBakeMeshGroup& group)
	{
		const MeshPtr mesh = MeshManager::Get().Load(group.mesh);
		if (!mesh)
		{
			WLOG("Glue scene: cannot load baked mesh " << group.mesh);
			return;
		}

		// The effective material of every submesh: its override, else the mesh's own.
		std::vector<MaterialPtr> materials(mesh->GetSubMeshCount());
		bool batchable = WorldModelInstance::CanBatchMesh(*mesh);
		for (uint16 i = 0; i < mesh->GetSubMeshCount(); ++i)
		{
			if (i < group.materialOverrides.size() && !group.materialOverrides[i].empty())
			{
				materials[i] = MaterialManager::Get().Load(group.materialOverrides[i]);
				if (!materials[i])
				{
					WLOG("Glue scene: cannot load material override " << group.materialOverrides[i] << " of " << group.mesh);
				}
			}

			if (!materials[i])
			{
				materials[i] = mesh->GetSubMesh(i).GetMaterial();
			}

			if (!WorldModelInstance::MaterialSupportsInstancing(materials[i]))
			{
				batchable = false;
			}
		}

		if (!batchable)
		{
			// Skinned, unindexed, or a material without an instanced shader variant (which would draw
			// nothing at all): one entity per placement.
			for (const size_t index : group.placements)
			{
				CreateBakeEntity(meshes[index].mesh, meshes[index].transform, meshes[index].materialOverrides);
			}
			return;
		}

		const uint64 triangles = CountTriangles(*mesh);
		for (uint16 i = 0; i < mesh->GetSubMeshCount(); ++i)
		{
			auto batch = std::make_shared<WorldModelBatch>("GlueBakeBatch_" + std::to_string(m_bakeObjectCounter++), mesh, i, materials[i]);
			for (const size_t index : group.placements)
			{
				MeshInstanceData instance;
				instance.worldMatrix = meshes[index].transform;
				batch->AddInstance(instance);
			}

			// Uploaded before attaching, so the node's first bounds are the instances' bounds. The
			// node stays at the identity: instance matrices and bounds are world space already.
			batch->UploadInstances(GraphicsDevice::Get());

			SceneNode* node = m_worldRootNode->CreateChildSceneNode();
			node->AttachObject(*batch);
			batch->SetScene(m_scene.get());
			batch->SetQueryFlags(0);
			node->UpdateBounds();

			m_bakeBatches.push_back(BakeBatch{ std::move(batch), node });
		}

		m_bakeInstanceCount += static_cast<uint32>(group.placements.size());
		m_bakeTriangleCount += triangles * group.placements.size();
	}

	Entity* GlueScene::CreateBakeEntity(const String& meshName, const Matrix4& transform, const std::vector<String>& materialOverrides)
	{
		const MeshPtr mesh = MeshManager::Get().Load(meshName);
		if (!mesh)
		{
			WLOG("Glue scene: cannot load baked mesh " << meshName);
			return nullptr;
		}

		Vector3 position;
		Vector3 scale;
		Quaternion orientation;
		transform.Decomposition(position, scale, orientation);

		SceneNode* node = m_worldRootNode->CreateChildSceneNode();
		node->SetPosition(position);
		node->SetOrientation(orientation);
		node->SetScale(scale);

		Entity* entity = m_scene->CreateEntity("GlueBakeEntity_" + std::to_string(m_bakeObjectCounter++), mesh);
		node->AttachObject(*entity);

		// Nothing in the glue scene is picked or collided with but the plaza characters.
		entity->SetQueryFlags(0);

		// The bake only holds what the glue cameras can see: no distance limit.
		entity->SetRenderingDistance(0.0f);

		const uint32 subEntityCount = entity->GetNumSubEntities();
		for (size_t i = 0; i < materialOverrides.size() && i < subEntityCount; ++i)
		{
			if (materialOverrides[i].empty())
			{
				continue;
			}

			if (const MaterialPtr material = MaterialManager::Get().Load(materialOverrides[i]))
			{
				entity->GetSubEntity(static_cast<uint16>(i))->SetMaterial(material);
			}
			else
			{
				WLOG("Glue scene: cannot load material override " << materialOverrides[i] << " of " << meshName);
			}
		}

		++m_bakeEntityCount;
		m_bakeTriangleCount += CountTriangles(*mesh);
		return entity;
	}

	void GlueScene::CreateBakeTerrainTile(const GlueBakeTerrainTile& tile)
	{
		Entity* entity = CreateBakeEntity(tile.mesh, Matrix4::Identity, {});
		if (!entity)
		{
			return;
		}

		MaterialPtr material = MaterialManager::Get().Load(tile.material);
		if (!material)
		{
			WLOG("Glue scene: cannot load terrain material " << tile.material << " of " << tile.mesh);
			return;
		}

		if (!tile.splatting.empty())
		{
			// A near tile: the layered terrain material with the tile's layer coverage, sampled the
			// way terrain::Tile samples its coverage texture (the .htex carries no sampler state, and
			// the default Wrap would bleed the opposite edge into the tile border).
			const TexturePtr splatting = TextureManager::Get().CreateOrRetrieve(tile.splatting);
			if (!splatting)
			{
				WLOG("Glue scene: cannot load splatting texture " << tile.splatting);
				return;
			}

			splatting->SetTextureAddressMode(TextureAddressMode::Clamp);
			splatting->SetFilter(TextureFilter::Anisotropic);

			auto instance = std::make_shared<MaterialInstance>("GlueBakeTerrain_" + std::to_string(m_bakeObjectCounter++), material);
			instance->SetTextureParameter("Splatting", splatting);
			entity->SetMaterial(instance);
			return;
		}

		// A far tile: its ready material instance, with the albedo clamped as FarTerrain does.
		String albedoName;
		if (material->GetTextureParameter("Albedo", albedoName) && !albedoName.empty())
		{
			if (const TexturePtr albedo = TextureManager::Get().CreateOrRetrieve(albedoName))
			{
				albedo->SetTextureAddressMode(TextureAddressMode::Clamp);
			}
		}

		entity->SetMaterial(material);
	}

	void GlueScene::CreateBakeFoliage(const std::vector<GlueBakeFoliage>& foliage)
	{
		if (foliage.empty())
		{
			return;
		}

		// The same hardware-instanced cells the world draws its trees with.
		m_bakeFoliage = std::make_unique<InstancedFoliage>(*m_scene, GraphicsDevice::Get(), bakeBatchCellSize);
		for (size_t i = 0; i < foliage.size(); ++i)
		{
			InstancedFoliageInstance instance;
			instance.uniqueId = static_cast<uint64>(i) + 1;
			instance.meshName = foliage[i].mesh;
			instance.position = foliage[i].position;
			instance.rotation = foliage[i].rotation;
			instance.scale = foliage[i].scale;
			instance.collides = false;
			m_bakeFoliage->AddInstance(instance);
		}

		m_bakeFoliage->RebuildDirtyCells();
	}

	void GlueScene::CreateBakeLight(const GlueBakeLight& bakeLight)
	{
		// Mirrors WorldModelInstance::CreateLightFrom; the bake drops ambient lights already.
		LightType type = LightType::Point;
		switch (bakeLight.type)
		{
		case 1:
			type = LightType::Spot;
			break;
		case 2:
			type = LightType::Directional;
			break;
		case 3:
			return;
		default:
			break;
		}

		const String name = "GlueBakeLight_" + std::to_string(m_bakeObjectCounter++);
		SceneNode* node = m_worldRootNode->CreateChildSceneNode(name + "_node");
		node->SetPosition(bakeLight.position);

		if (type != LightType::Point)
		{
			Quaternion rotation = bakeLight.rotation;
			const float lengthSq = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
			if (lengthSq > 0.001f)
			{
				rotation.Normalize();
				node->SetOrientation(rotation);
			}
		}

		Light& light = m_scene->CreateLight(name, type);

		// 0xAARRGGBB.
		const float r = static_cast<float>((bakeLight.color >> 16) & 0xFF) / 255.0f;
		const float g = static_cast<float>((bakeLight.color >> 8) & 0xFF) / 255.0f;
		const float b = static_cast<float>(bakeLight.color & 0xFF) / 255.0f;
		light.SetColor(Vector4(r, g, b, 1.0f));
		light.SetIntensity(std::min(bakeLight.intensity, 10.0f));
		light.SetRange(bakeLight.useAttenuation && bakeLight.attenuationEnd > 0.0f ? bakeLight.attenuationEnd : 50.0f);

		if (type == LightType::Spot)
		{
			light.SetOuterConeAngle(bakeLight.outerConeAngle);
			light.SetInnerConeAngle(bakeLight.innerConeAngle);
		}

		light.SetFogScattering(bakeLight.fogScattering);
		node->AttachObject(light);
	}

	void GlueScene::CreateBakeParticles(const GlueBakeParticles& particles)
	{
		const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(particles.file);
		if (!file)
		{
			WLOG("Glue scene: cannot open particle system " << particles.file);
			return;
		}

		io::StreamSource source(*file);
		io::Reader reader(source);
		ParticleEmitterSerializer serializer;
		ParticleEmitterParameters parameters;
		if (!serializer.Deserialize(parameters, reader))
		{
			WLOG("Glue scene: cannot read particle system " << particles.file);
			return;
		}

		const String name = "GlueBakeParticles_" + std::to_string(m_bakeObjectCounter++);
		ParticleEmitter* emitter = m_scene->CreateParticleEmitter(name);
		if (!emitter)
		{
			return;
		}

		emitter->SetParameters(parameters);

		Vector3 position;
		Vector3 scale;
		Quaternion orientation;
		particles.transform.Decomposition(position, scale, orientation);

		SceneNode* node = m_worldRootNode->CreateChildSceneNode(name + "_node");
		node->SetPosition(position);
		node->SetOrientation(orientation);
		node->SetScale(scale);
		node->AttachObject(*emitter);
		emitter->Play();
	}

	bool GlueScene::LoadWorld()
	{
		const String& assetPath = m_definition.world;

		m_worldInstance = std::make_shared<ClientWorldInstance>(*m_scene, *m_worldRootNode, assetPath, m_workQueue, m_dispatcher);
		m_worldInstance->SetAmbientSounds(&m_ambientSounds);

		const std::unique_ptr<std::istream> streamPtr = AssetRegistry::OpenFile(assetPath + ".hwld");
		if (!streamPtr)
		{
			ELOG("Glue scene: failed to open world file '" << assetPath << ".hwld'");
			return false;
		}

		io::StreamSource source{ *streamPtr };
		io::Reader reader{ source };

		ClientWorldInstanceDeserializer deserializer{ *m_worldInstance };
		if (!deserializer.Read(reader))
		{
			ELOG("Glue scene: failed to read world '" << assetPath << ".hwld'");
			return false;
		}

		return true;
	}

	void GlueScene::Update(const float deltaSeconds)
	{
		PROFILE_SCOPE("GlueScene::Update");

		if (m_useBake)
		{
			UpdateBakeLoading();
		}
		else
		{
			UpdatePageLoading();
		}

		// The bake path has no world instance, so nothing is ever posted to the dispatcher.
		if (!m_useBake)
		{
			PROFILE_SCOPE("GlueScene::StreamingDispatch");

			const bool covered = !m_gateReady || (m_pendingArrival != PendingArrival::None && !m_plazaReady);
			const double budgetMs = covered ? coveredDispatchBudgetMs : dispatchBudgetMs;

			const auto start = std::chrono::steady_clock::now();
			while (m_dispatcher.poll_one())
			{
				if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= budgetMs)
				{
					break;
				}
			}
		}

		m_director.Update(deltaSeconds);
		if (m_director.ConsumeFlightFinished())
		{
			m_flightFinished = true;
		}

		// The plaza only becomes ready after the gate (or when both readiness timeouts fired), so
		// this also means the gate view the fly-in starts from is ready.
		if (m_pendingArrival != PendingArrival::None && m_plazaReady)
		{
			if (m_pendingArrival == PendingArrival::FlyIn)
			{
				// The list beat the scene on a fresh login: the fade held until now, and the flight
				// starts from the gate as it lifts.
				m_director.SnapToGate();
				m_director.FlyIn();
				m_deferredFlightStarted = true;
			}
			else
			{
				// Back from the world, or a skip arrived while waiting: cut straight to the plaza.
				m_director.SnapToPlaza();
				m_flightFinished = true;
			}

			m_pendingArrival = PendingArrival::None;
		}

		UpdateCamera(deltaSeconds);

		const Vector3 cameraPosition = m_camera->GetDerivedPosition();

		m_plazaAvatars->Update(deltaSeconds, cameraPosition);
		if (m_worldInstance && m_worldInstance->HasTerrain())
		{
			m_worldInstance->GetTerrain()->NotifyCameraPosition(cameraPosition);
		}

		m_ambientSounds.Update(cameraPosition, deltaSeconds);

		m_sky->Update(deltaSeconds, GetAsyncTimeMs());
		m_sky->SetPosition(cameraPosition);
		m_environment.Update(deltaSeconds, m_sky->GetNormalizedTimeOfDay());
		m_sky->ApplyEnvironment(m_environment.GetState());

		if (m_renderer)
		{
			if (m_exposureVar == nullptr)
			{
				// Looked up until it is registered, then kept: unregistering only flags a cvar, so
				// the pointer stays valid (IsValid tells whether it is registered right now).
				m_exposureVar = ConsoleVarMgr::FindConsoleVar("gxExposure");
			}

			const float brightness = (m_exposureVar && m_exposureVar->IsValid()) ? m_exposureVar->GetFloatValue() : 1.0f;
			ApplyEnvironmentStateToRenderer(*m_renderer, m_environment.GetState(), brightness);
		}
	}

	void GlueScene::UpdatePageLoading()
	{
		ApplyReadinessTimeout();

		// Every page (gate pages first) is loaded before the gate counts as ready: the gate camera
		// looks across the plaza pages too, and the world models it sees there must not pop in
		// after the fade lifted. One page per update; their entities load in the background.
		if (m_nextPage < m_pageQueue.size())
		{
			const auto [x, y] = m_pageQueue[m_nextPage++];
			LoadPage(x, y);
			if (m_nextPage >= m_pageQueue.size())
			{
				m_terrainLoadedMs = GetAsyncTimeMs();
			}
			return;
		}

		if (!m_gateReady)
		{
			if (!m_worldInstance || !HasPendingGateContent())
			{
				MarkGateReady(false);
			}

			return;
		}

		if (!m_plazaReady)
		{
			if (!m_worldInstance || !m_worldInstance->HasPendingEntityLoads())
			{
				m_plazaReady = true;
				DLOG("Glue scene: plaza pages loaded after " << (GetAsyncTimeMs() - m_plazaLoadStartMs) << " ms");
			}
		}
	}

	void GlueScene::ApplyReadinessTimeout()
	{
		const GameTime now = GetAsyncTimeMs();

		if (!m_pageLoadingStarted)
		{
			// The pages load one per Update, so the gate's clock starts with the first one, not at
			// construction (the login screen may take a while to set up before its first frame).
			m_pageLoadingStarted = true;
			m_gateLoadStartMs = now;
		}

		if (!m_gateReady)
		{
			if (now - m_gateLoadStartMs >= readinessTimeoutMs)
			{
				WLOG("Glue scene: gate view not loaded after " << (now - m_gateLoadStartMs) << " ms, showing it anyway");
				MarkGateReady(true);
			}

			// The plaza's clock starts when the gate is ready.
			return;
		}

		if (!m_plazaReady && now - m_plazaLoadStartMs >= readinessTimeoutMs)
		{
			WLOG("Glue scene: plaza not loaded after " << (now - m_plazaLoadStartMs) << " ms, showing it anyway");
			m_plazaReady = true;
		}
	}

	void GlueScene::MarkGateReady(const bool timedOut)
	{
		m_gateReady = true;

		const GameTime now = GetAsyncTimeMs();
		m_plazaLoadStartMs = now;

		if (m_useBake)
		{
			ILOG("Glue scene: gate ready after " << (now - m_gateLoadStartMs) << " ms (bake" << (timedOut ? ", timeout" : "") << ")");
			return;
		}

		if (timedOut)
		{
			// Neither the terrain nor the models finished: a split would be misleading.
			ILOG("Glue scene: gate ready after " << (now - m_gateLoadStartMs) << " ms (timeout, "
				<< (m_terrainLoadedMs != 0 ? "terrain done, models pending" : "terrain pending") << ")");
			return;
		}

		// Terrain: until the last page was loaded (entity files still reading in the background).
		// Models: from then until the world models the gate camera sees were created.
		const GameTime terrainDoneMs = m_terrainLoadedMs != 0 ? m_terrainLoadedMs : now;
		ILOG("Glue scene: gate ready after " << (now - m_gateLoadStartMs) << " ms (terrain "
			<< (terrainDoneMs - m_gateLoadStartMs) << " ms, models " << (now - terrainDoneMs) << " ms)");
	}

	bool GlueScene::HasPendingGateContent() const
	{
		if (m_director.GetMode() != GlueCameraMode::Gate)
		{
			// Only the gate camera's view can be tested: anything still loading may show.
			return m_worldInstance->HasPendingEntityLoads();
		}

		const Camera& camera = *m_camera;
		return m_worldInstance->HasPendingWorldModels([&camera](const AABB& bounds)
			{
				return camera.IsVisible(bounds);
			});
	}

	void GlueScene::LoadPage(const uint8 x, const uint8 y)
	{
		PROFILE_SCOPE("GlueScene::LoadPage");

		if (!m_worldInstance)
		{
			return;
		}

		if (m_worldInstance->HasTerrain())
		{
			if (terrain::Page* page = m_worldInstance->GetTerrain()->GetPage(x, y))
			{
				// Nothing else prepares pages of this terrain, so the preparation can always run
				// synchronously here (WorldState::WaitForWorldLoading without the in-flight case).
				if (!page->IsPrepared() && page->BeginPrepare())
				{
					if (page->PrepareParse())
					{
						page->FinalizePrepare();
					}
					else
					{
						page->AbortPrepare();
						WLOG("Glue scene: failed to prepare terrain page " << static_cast<int32>(x) << "," << static_cast<int32>(y));
					}
				}

				if (page->IsPrepared())
				{
					while (!page->Load())
					{
					}
				}
			}
		}

		m_worldInstance->LoadPageEntities(x, y);
	}

	void GlueScene::UpdateCamera(const float deltaSeconds)
	{
		float pathParameter = m_director.GetPathParameter();
		if (s_glueCameraPathVar && s_glueCameraPathVar->GetFloatValue() >= 0.0f)
		{
			pathParameter = s_glueCameraPathVar->GetFloatValue();
		}

		const GlueCameraKey key = EvaluateGluePath(m_definition.flightPath, pathParameter);
		Vector3 position = key.position;

		if (m_director.GetMode() == GlueCameraMode::Gate)
		{
			m_driftSeconds = std::fmod(m_driftSeconds + deltaSeconds, driftPeriodSeconds);

			// Sway sideways, across the view direction.
			Vector3 right = (key.lookAt - key.position).Cross(Vector3::UnitY);
			if (right.GetSquaredLength() > 0.0001f)
			{
				right.Normalize();
				position += right * (driftAmplitude * std::sin(m_driftSeconds / driftPeriodSeconds * 2.0f * Pi));
			}
		}

		if (m_pushInSeconds > 0.0f)
		{
			if (m_pushInElapsed < m_pushInSeconds)
			{
				m_pushInElapsed = std::min(m_pushInElapsed + deltaSeconds, m_pushInSeconds);
				if (m_pushInElapsed >= m_pushInSeconds)
				{
					m_pushInFinished = true;
				}
			}

			const float t = m_pushInElapsed / m_pushInSeconds;
			const float eased = t * t * (3.0f - 2.0f * t);
			const Vector3 target = m_pushInCharacterPosition + Vector3(0.0f, pushInTargetHeight, 0.0f);
			position += (target - position) * (pushInFraction * eased);
		}

		m_cameraNode->SetPosition(position);
		m_cameraNode->LookAt(key.lookAt, TransformSpace::World, Vector3::NegativeUnitZ);
	}
}
