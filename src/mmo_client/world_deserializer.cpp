
#include "world_deserializer.h"

#include <filesystem>
#include <set>
#include <utility>

#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "console/console_var.h"
#include "game_common/world_entity_loader.h"
#include "game_common/world_foliage.h"
#include "game_states/login_state.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "scene_graph/entity.h"
#include "scene_graph/instanced_foliage.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/prop_kit_manager.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/scene.h"
#include "scene_graph/world_model_instance.h"
#include "scene_graph/world_model_manager.h"
#include "base/profiler.h"

namespace mmo
{
	static const ChunkMagic VersionChunkMagic = MakeChunkMagic('MVER');
	static const ChunkMagic MeshNamesChunkMagic = MakeChunkMagic('MESH');
	static const ChunkMagic EntityChunkMagic = MakeChunkMagic('MENT');
	static const ChunkMagic TerrainChunkMagic = MakeChunkMagic('RRET');

	namespace
	{
		ConsoleVar* GetWorldModelMergingVar()
		{
			return ConsoleVarMgr::RegisterConsoleVar("gxWorldModelMerging",
				"Whether each world model group's static geometry is merged into one mesh per material (a few draw calls per group). Takes effect for world models loaded afterwards.", "1");
		}
	}

	struct ClientWorldInstance::WorldModelPreparation
	{
		uint16 pageIndex = 0;
		WorldEntityLoader::MapEntity entity;
		bool merging = true;

		/// World models still to load: the placed one, then its children, depth first.
		std::vector<String> pendingModels;
		std::set<String> knownModels;

		/// Loaded world models, in load order, waiting for their merged geometry.
		std::vector<WorldModelPtr> models;
		size_t nextMerge = 0;

		/// Keeps the merged geometry alive until the instance holds it itself.
		std::vector<std::shared_ptr<const void>> mergedGeometry;

		std::vector<String> pendingKits;

		std::vector<String> pendingMeshes;
		std::set<String> knownMeshes;

		void QueueMeshes(const std::vector<String>& meshPaths)
		{
			for (const String& path : meshPaths)
			{
				if (knownMeshes.insert(path).second)
				{
					pendingMeshes.push_back(path);
				}
			}
		}
	};

	ClientWorldInstanceDeserializer::ClientWorldInstanceDeserializer(ClientWorldInstance& world)
		: ChunkReader()
		, m_world(world)
	{
		m_ignoreUnhandledChunks = true;

		AddChunkHandler(*VersionChunkMagic, true, *this, &ClientWorldInstanceDeserializer::ReadVersionChunk);
	}

	ClientWorldInstance::~ClientWorldInstance()
	{
		// Ensure all entities are unloaded properly
		for (auto it = m_entities.begin(); it != m_entities.end(); ++it)
		{
			Entity* entity = it->second.entity;
			SceneNode* node = it->second.node;
			entity->DetachFromParent();
			m_scene.DestroyEntity(*entity);
			m_scene.DestroySceneNode(*node);
		}
		m_entities.clear();

		// Ensure all world models are unloaded properly
		for (auto it = m_worldModels.begin(); it != m_worldModels.end(); ++it)
		{
			if (it->second.instance)
			{
				it->second.instance->Destroy(m_scene);
				it->second.instance->DetachFromParent();
			}
			if (it->second.node)
			{
				m_scene.DestroySceneNode(*it->second.node);
			}
		}
		m_worldModels.clear();
	}

	ClientWorldInstance::ClientWorldInstance(Scene& scene, SceneNode& rootNode, const String& name, asio::io_service& workQueue, asio::io_service& dispatcher)
		: m_workQueue(workQueue)
		, m_dispatcher(dispatcher)
		, m_name(name)
		, m_scene(scene)
		, m_rootNode(rootNode)
	{
		// Authored foliage (trees) is rendered through hardware-instanced cells that are a few
		// terrain tiles wide so that each cell batches enough instances to be worth instancing.
		m_foliage = std::make_unique<InstancedFoliage>(scene, GraphicsDevice::Get(), static_cast<float>(terrain::constants::PageSize) / 4.0f);

		// Keep the merged geometry of world models that streamed out, so walking back into a town does
		// not merge it again (GPU readbacks of every mesh included). The source meshes stay cached anyway.
		WorldModelInstance::SetMergedGeometryRetention(256 * 1024 * 1024);
	}

	void ClientWorldInstance::LoadPageEntities(uint8 x, uint8 y)
	{
		const uint16 pageIndex = BuildPageIndex(x, y);

		String baseFileName = (std::filesystem::path(m_name) / "Entities" / std::to_string(pageIndex)).string();
		std::transform(baseFileName.begin(), baseFileName.end(), baseFileName.begin(), [](char c) { return c == '\\' ? '/' : c; });

		auto files = AssetRegistry::ListFiles(baseFileName, ".wobj");

		for (const auto& file : files)
		{
			std::weak_ptr weak = weak_from_this();
			++m_pendingEntityFiles;
			m_workQueue.post([weak, pageIndex, file]()
				{
					const auto strong = weak.lock();
					if (!strong)
					{
						return;
					}

					strong->InternalLoadPageEntity(pageIndex, file);
				});
		}

		m_loadedPages.insert(pageIndex);

		LoadPageFoliage(x, y);
	}

	void ClientWorldInstance::UnloadPageEntities(uint8 x, uint8 y)
	{
		PROFILE_SCOPE("Stream: UnloadPageEntities");
		const uint16 pageIndex = BuildPageIndex(x, y);

		// Unload regular entities
		for (auto it = m_entities.begin(); it != m_entities.end();)
		{
			if (it->second.pageIndex != pageIndex)
			{
				++it;
				continue;
			}

			Entity* entity = it->second.entity;
			SceneNode* node = it->second.node;
			entity->DetachFromParent();
			m_scene.DestroyEntity(*entity);
			m_scene.DestroySceneNode(*node);
			it = m_entities.erase(it);
		}

		// Unload world models
		for (auto it = m_worldModels.begin(); it != m_worldModels.end();)
		{
			if (it->second.pageIndex != pageIndex)
			{
				++it;
				continue;
			}

			if (it->second.instance)
			{
				it->second.instance->Destroy(m_scene);
				it->second.instance->DetachFromParent();
			}
			if (it->second.node)
			{
				m_scene.DestroySceneNode(*it->second.node);
			}
			it = m_worldModels.erase(it);
		}

		if (m_foliage)
		{
			m_foliage->UnloadPage(pageIndex);
		}

		m_loadedPages.erase(BuildPageIndex(x, y));
	}

	void ClientWorldInstance::UnloadAllEntities()
	{
		// Unload regular entities
		for (auto it = m_entities.begin(); it != m_entities.end();)
		{
			Entity* entity = it->second.entity;
			SceneNode* node = it->second.node;
			entity->DetachFromParent();
			m_scene.DestroyEntity(*entity);
			m_scene.DestroySceneNode(*node);
			it = m_entities.erase(it);
		}

		// Unload world models
		for (auto it = m_worldModels.begin(); it != m_worldModels.end();)
		{
			if (it->second.instance)
			{
				it->second.instance->Destroy(m_scene);
				it->second.instance->DetachFromParent();
			}
			if (it->second.node)
			{
				m_scene.DestroySceneNode(*it->second.node);
			}
			it = m_worldModels.erase(it);
		}

		if (m_foliage)
		{
			m_foliage->Clear();
		}

		m_loadedPages.clear();
	}

	Entity* ClientWorldInstance::CreateMapEntity(const String& meshName, const Vector3& position, const Quaternion& orientation, const Vector3& scale, uint64 uniqueId)
	{
		PROFILE_SCOPE("Stream: CreateMapEntity");
		const String entityName = "Entity_" + std::to_string(uniqueId);
		if (m_scene.HasEntity(entityName))
		{
			return m_scene.GetEntity(entityName);
		}

		// Create scene node
		SceneNode* node = m_rootNode.CreateChildSceneNode();
		node->SetPosition(position);
		node->SetOrientation(orientation);
		node->SetScale(scale);

		// Create entity
		Entity* entity = m_scene.CreateEntity(entityName, meshName);
		node->AttachObject(*entity);
		entity->SetQueryFlags(1 | (1 << 6));

		// TODO: Save distance or take from mesh
		entity->SetRenderingDistance(256.0f);

		const PagePosition page = GetPagePosition(position);
		m_entities.emplace(uniqueId, EntityPlacement{ .pageIndex= BuildPageIndex(page.x(), page.y()), .entity= entity, .node= node });

		return entity;
	}

	WorldModelInstance* ClientWorldInstance::CreateWorldModelEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale, uint64 uniqueId, const std::vector<String>& kitPaths)
	{
		PROFILE_SCOPE("Stream: CreateWorldModelEntity");
		// Check if already exists
		auto it = m_worldModels.find(uniqueId);
		if (it != m_worldModels.end())
		{
			return it->second.instance.get();
		}

		const String instanceName = "WorldModel_" + std::to_string(uniqueId);

		// Load the world model
		WorldModelPtr worldModel = WorldModelManager::Get().Load(assetName);
		if (!worldModel)
		{
			ELOG("Failed to load world model: " << assetName);
			return nullptr;
		}

		// Create the world model instance
		auto worldModelInstance = std::make_unique<WorldModelInstance>(instanceName, worldModel);
		worldModelInstance->SetQueryFlags(1 | (1 << 6));

		// Hardware-instanced world model geometry (draw-call batching). Client-only: both editors
		// keep the per-entity path, because the world editor picks world models by hitting a child
		// entity and its transform gizmo needs those entities to exist.
		//
		// Must be set before the node attach below - geometry is created on first attach.
		if (const ConsoleVar* batchVar = ConsoleVarMgr::RegisterConsoleVar("gxWorldModelBatching",
			"Whether world model geometry is rendered with hardware instancing instead of one draw call per mesh reference. Takes effect for world models loaded afterwards.", "1"))
		{
			worldModelInstance->SetBatchingEnabled(batchVar->GetBoolValue());
		}

		// Static geometry merging: each group's meshes baked into one mesh per material. Takes
		// precedence over batching where both are on.
		if (const ConsoleVar* mergeVar = GetWorldModelMergingVar())
		{
			worldModelInstance->SetMergingEnabled(mergeVar->GetBoolValue());
		}

		// Prop kits are loaded here, on the main thread (this runs from the dispatcher-posted
		// lambda), because resource managers must not be touched from the loader thread. They must
		// be set before the node attach below - kit content is built with the geometry on first attach.
		if (!kitPaths.empty())
		{
			std::vector<PropKitPtr> kits;
			kits.reserve(kitPaths.size());
			for (const String& kitPath : kitPaths)
			{
				PropKitPtr kit = PropKitManager::Get().Load(kitPath);
				if (!kit)
				{
					WLOG("Placement " << uniqueId << ": prop kit " << kitPath << " failed to load");
					continue;
				}
				kits.push_back(std::move(kit));
			}
			worldModelInstance->SetPropKits(std::move(kits));
		}

		// Create scene node
		SceneNode* node = m_rootNode.CreateChildSceneNode();
		node->SetPosition(position);
		node->SetOrientation(orientation);
		node->SetScale(scale);
		node->AttachObject(*worldModelInstance);

		// Get the raw pointer before moving ownership
		WorldModelInstance* rawInstance = worldModelInstance.get();

		const PagePosition page = GetPagePosition(position);
		m_worldModels.emplace(uniqueId, WorldModelPlacement{
			.pageIndex = BuildPageIndex(page.x(), page.y()),
			.instance = std::move(worldModelInstance),
			.node = node
		});

		return rawInstance;
	}

	void ClientWorldInstance::InternalLoadPageEntity(uint16 pageIndex, const String& filename)
	{
		std::unique_ptr<std::istream> filePtr = AssetRegistry::OpenFile(filename);
		if (!filePtr)
		{
			ELOG("Failed to open file " << filename << "!");
			--m_pendingEntityFiles;
			return;
		}

		io::StreamSource source{ *filePtr };
		io::Reader reader{ source };
		WorldEntityLoader loader;
		if (!loader.Read(reader))
		{
			ELOG("Failed to read file " << filename << "!");
			--m_pendingEntityFiles;
			return;
		}

		const auto& entity = loader.GetEntity();

		std::weak_ptr weak = weak_from_this();
		m_dispatcher.post([weak, pageIndex, entity]()
			{
				auto strong = weak.lock();
				if (!strong)
				{
					return;
				}

				--strong->m_pendingEntityFiles;

				if (!strong->m_loadedPages.contains(pageIndex))
				{
					return;
				}

				// Handle different entity types
				if (entity.entityType == WorldEntityType::WorldModel)
				{
					// Loads its resources over several steps before the instance is created
					strong->BeginWorldModelEntity(pageIndex, entity);
				}
				else
				{
					// Create regular mesh entity
					Entity* object = strong->CreateMapEntity(
						entity.meshName,
						entity.position,
						entity.rotation,
						entity.scale,
						entity.uniqueId
					);

					if (object)
					{
						// Apply material overrides
						for (const auto& materialOverride : entity.materialOverrides)
						{
							if (materialOverride.materialIndex >= object->GetNumSubEntities())
							{
								WLOG("Entity has material override for material index greater than entity material count! Skipping material override");
								continue;
							}

							object->GetSubEntity(materialOverride.materialIndex)->SetMaterial(MaterialManager::Get().Load(materialOverride.materialName));
						}
					}
				}
			});
	}

	void ClientWorldInstance::BeginWorldModelEntity(const uint16 pageIndex, const WorldEntityLoader::MapEntity& entity)
	{
		if (m_worldModels.contains(entity.uniqueId))
		{
			return;
		}

		auto preparation = std::make_shared<WorldModelPreparation>();
		preparation->pageIndex = pageIndex;
		preparation->entity = entity;
		preparation->pendingModels.push_back(entity.meshName);
		preparation->knownModels.insert(entity.meshName);
		preparation->pendingKits = entity.kitPaths;
		if (const ConsoleVar* mergeVar = GetWorldModelMergingVar())
		{
			preparation->merging = mergeVar->GetBoolValue();
		}

		// Released by ContinueWorldModelEntity once the instance exists or the preparation is dropped
		++m_pendingWorldModels;
		ContinueWorldModelEntity(preparation);
	}

	void ClientWorldInstance::PostPreparationStep(const std::shared_ptr<WorldModelPreparation>& preparation)
	{
		std::weak_ptr weak = weak_from_this();
		m_dispatcher.post([weak, preparation]()
			{
				if (const auto strong = weak.lock())
				{
					strong->ContinueWorldModelEntity(preparation);
				}
			});
	}

	void ClientWorldInstance::ContinueWorldModelEntity(const std::shared_ptr<WorldModelPreparation>& preparation)
	{
		PROFILE_SCOPE("Stream: WorldModel step");

		// The page streamed out again before the placement was ready: whatever was loaded stays cached
		if (!m_loadedPages.contains(preparation->pageIndex))
		{
			--m_pendingWorldModels;
			return;
		}

		WorldModelPreparation& prep = *preparation;
		if (!prep.pendingModels.empty())
		{
			const String path = std::move(prep.pendingModels.back());
			prep.pendingModels.pop_back();

			// A missing child is reported by WorldModelManager and skipped by the instance as well
			if (WorldModelPtr model = WorldModelManager::Get().Load(path))
			{
				std::vector<String> meshPaths;
				WorldModelInstance::CollectMeshDependencies(*model, meshPaths);
				prep.QueueMeshes(meshPaths);

				for (const WorldModelChildRef& childRef : model->GetChildRefs())
				{
					if (childRef.visible && !childRef.wmoPath.empty() && prep.knownModels.insert(childRef.wmoPath).second)
					{
						prep.pendingModels.push_back(childRef.wmoPath);
					}
				}

				prep.models.push_back(std::move(model));
			}
		}
		else if (!prep.pendingKits.empty())
		{
			const String path = std::move(prep.pendingKits.back());
			prep.pendingKits.pop_back();

			if (const PropKitPtr kit = PropKitManager::Get().Load(path))
			{
				std::vector<String> meshPaths;
				for (const WorldModelMeshRef& prop : kit->props)
				{
					if (prop.visible && !prop.meshPath.empty())
					{
						meshPaths.push_back(prop.meshPath);
					}
				}
				prep.QueueMeshes(meshPaths);
			}
		}
		else if (!prep.pendingMeshes.empty())
		{
			// Meshes already in the cache cost nothing, so a step continues until one actually loads
			while (!prep.pendingMeshes.empty())
			{
				const String path = std::move(prep.pendingMeshes.back());
				prep.pendingMeshes.pop_back();

				const bool cached = MeshManager::Get().Find(path) != nullptr;
				MeshManager::Get().Load(path);
				if (!cached)
				{
					break;
				}
			}
		}
		else if (prep.merging && prep.nextMerge < prep.models.size())
		{
			// A group per step: a town shell alone merges for ~100 ms
			if (auto merged = WorldModelInstance::PrewarmMergedGeometry(prep.models[prep.nextMerge], 1))
			{
				prep.mergedGeometry.push_back(std::move(merged));
				++prep.nextMerge;
			}
		}
		else
		{
			// Everything is cached now, so this only creates the instances and their entities
			CreateWorldModelEntity(prep.entity.meshName, prep.entity.position, prep.entity.rotation, prep.entity.scale, prep.entity.uniqueId, prep.entity.kitPaths);
			--m_pendingWorldModels;
			return;
		}

		PostPreparationStep(preparation);
	}

	void ClientWorldInstance::LoadPageFoliage(const uint8 x, const uint8 y)
	{
		const uint16 pageIndex = BuildPageIndex(x, y);

		String fileName = (std::filesystem::path(m_name) / "Foliage" / (std::to_string(pageIndex) + ".hfol")).string();
		std::transform(fileName.begin(), fileName.end(), fileName.begin(), [](char c) { return c == '\\' ? '/' : c; });

		std::weak_ptr weak = weak_from_this();
		m_workQueue.post([weak, pageIndex, fileName]()
			{
				const auto strong = weak.lock();
				if (!strong)
				{
					return;
				}

				strong->InternalLoadPageFoliage(pageIndex, fileName);
			});
	}

	void ClientWorldInstance::InternalLoadPageFoliage(const uint16 pageIndex, const String& filename)
	{
		// A page without authored foliage simply has no file - that is not an error.
		std::unique_ptr<std::istream> filePtr = AssetRegistry::OpenFile(filename);
		if (!filePtr)
		{
			return;
		}

		io::StreamSource source{ *filePtr };
		io::Reader reader{ source };
		WorldFoliageLoader loader;
		if (!loader.Read(reader))
		{
			ELOG("Failed to read foliage file " << filename << "!");
			return;
		}

		auto instances = std::make_shared<std::vector<FoliageInstance>>(loader.TakeInstances());

		std::weak_ptr weak = weak_from_this();
		m_dispatcher.post([weak, pageIndex, instances]()
			{
				const auto strong = weak.lock();
				if (!strong || !strong->m_foliage)
				{
					return;
				}

				if (!strong->m_loadedPages.contains(pageIndex))
				{
					return;
				}

				for (const auto& source : *instances)
				{
					InstancedFoliageInstance instance;
					instance.uniqueId = source.uniqueId;
					instance.meshName = source.meshName;
					instance.position = source.position;
					instance.rotation = source.rotation;
					instance.scale = source.scale;
					instance.pageIndex = pageIndex;
					instance.collides = source.collides;
					strong->m_foliage->AddInstance(instance);
				}

				// Build the GPU buffers for the cells touched by this page on the main thread.
				strong->m_foliage->RebuildDirtyCells();
			});
	}

	bool ClientWorldInstanceDeserializer::ReadVersionChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		ASSERT(chunkHeader == *VersionChunkMagic);

		// Read version
		m_version = 0;
		reader >> io::read<uint32>(m_version);

		// Check if version is supported
		if (m_version >= world_version::Version_0_0_0_1)
		{
			AddChunkHandler(*MeshNamesChunkMagic, false, *this, &ClientWorldInstanceDeserializer::ReadMeshNamesChunk);
			AddChunkHandler(*TerrainChunkMagic, false, *this, &ClientWorldInstanceDeserializer::ReadTerrainChunk);
			return true;
		}

		ELOG("Unsupported world version: " << m_version);
		return false;
	}

	bool ClientWorldInstanceDeserializer::ReadMeshNamesChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		ASSERT(chunkHeader == *MeshNamesChunkMagic);

		RemoveChunkHandler(*MeshNamesChunkMagic);

		if (m_version == world_version::Version_0_0_0_1)
		{
			AddChunkHandler(*EntityChunkMagic, false, *this, &ClientWorldInstanceDeserializer::ReadEntityChunk);
		}
		else if (m_version == world_version::Version_0_0_0_2)
		{
			AddChunkHandler(*EntityChunkMagic, false, *this, &ClientWorldInstanceDeserializer::ReadEntityChunkV2);
		}

		if (!m_meshNames.empty())
		{
			ELOG("Duplicate mesh names chunk detected!");
			return false;
		}

		const size_t contentStart = reader.getSource()->position();
		while (reader.getSource()->position() - contentStart < chunkSize)
		{
			String meshName;
			if (!(reader >> io::read_string(meshName)))
			{
				ELOG("Failed to read world file: Unexpected end of file");
				return false;
			}

			m_meshNames.emplace_back(std::move(meshName));
		}

		return reader;
	}

	bool ClientWorldInstanceDeserializer::ReadEntityChunk(io::Reader& reader, const uint32 chunkHeader, const uint32 chunkSize)
	{
		ASSERT(chunkHeader == *EntityChunkMagic);

		if (m_meshNames.empty())
		{
			ELOG("No mesh names known, can't read entity chunks before mesh chunk!");
			return false;
		}

		if (chunkSize != sizeof(MapEntityChunkContent))
		{
			ELOG("Entity chunk has incorrect chunk size, found " << log_hex_digit(chunkSize) << " bytes, expected " << log_hex_digit(sizeof(MapEntityChunkContent)) << " bytes");
			return false;
		}

		MapEntityChunkContent content;
		reader.readPOD(content);
		if (!reader)
		{
			ELOG("Failed to read map entity chunk content, unexpected end of file!");
			return false;
		}

		if (content.meshNameIndex >= m_meshNames.size())
		{
			ELOG("Map entity chunk references unknown mesh names!");
			return false;
		}

		m_world.CreateMapEntity(m_meshNames[content.meshNameIndex], content.position, content.rotation, content.scale, content.uniqueId);
		return reader;
	}

	bool ClientWorldInstanceDeserializer::ReadEntityChunkV2(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *EntityChunkMagic);

		if (m_meshNames.empty())
		{
			ELOG("No mesh names known, can't read entity chunks before mesh chunk!");
			return false;
		}

		uint32 uniqueId;
		uint32 meshNameIndex;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale;
		if (!(reader 
			>> io::read<uint32>(uniqueId) 
			>> io::read<uint32>(meshNameIndex) 
			>> io::read<float>(position.x)
			>> io::read<float>(position.y)
			>> io::read<float>(position.z)
			>> io::read<float>(rotation.w)
			>> io::read<float>(rotation.x)
			>> io::read<float>(rotation.y)
			>> io::read<float>(rotation.z)
			>> io::read<float>(scale.x)
			>> io::read<float>(scale.y)
			>> io::read<float>(scale.z)
			))
		{
			ELOG("Failed to read map entity chunk content, unexpected end of file!");
			return false;
		}

		ASSERT(position.IsValid());
		ASSERT(!rotation.IsNaN());
		ASSERT(scale.IsValid());

		if (meshNameIndex >= m_meshNames.size())
		{
			ELOG("Map entity chunk references unknown mesh names!");
			return false;
		}

		struct MaterialOverride
		{
			uint8 materialIndex;
			String materialName;
		};

		std::vector<MaterialOverride> materialOverrides;

		uint8 numMaterialOverrides;
		if (!(reader >> io::read<uint8>(numMaterialOverrides)))
		{
			ELOG("Failed to read material override count for map entity chunk, unexpected end of file!");
			return false;
		}

		materialOverrides.resize(numMaterialOverrides);
		for (uint8 i = 0; i < numMaterialOverrides; ++i)
		{
			if (!(reader >> io::read<uint8>(materialOverrides[i].materialIndex) >> io::read_container<uint16>(materialOverrides[i].materialName)))
			{
				ELOG("Failed to read material override for map entity chunk, unexpected end of file!");
				return false;
			}
		}

		if (Entity* entity = m_world.CreateMapEntity(m_meshNames[meshNameIndex], position, rotation, scale, uniqueId))
		{
			// Apply material overrides
			for (const auto& materialOverride : materialOverrides)
			{
				if (materialOverride.materialIndex >= entity->GetNumSubEntities())
				{
					WLOG("Entity has material override for material index greater than entity material count! Skipping material override");
					continue;
				}

				entity->GetSubEntity(materialOverride.materialIndex)->SetMaterial(MaterialManager::Get().Load(materialOverride.materialName));
			}
		}

		return reader;
	}

	bool ClientWorldInstanceDeserializer::ReadTerrainChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *TerrainChunkMagic);

		uint8 hasTerrain;
		if (!(reader >> io::read<uint8>(hasTerrain)))
		{
			ELOG("Failed to read terrain chunk: Unexpected end of file");
			return false;
		}

		if (hasTerrain)
		{
			// Ensure terrain is created
			m_world.m_terrain = std::make_unique<terrain::Terrain>(m_world.m_scene, nullptr, 64, 64);
			m_world.m_terrain->SetBaseFileName(m_world.m_name + "/Terrain");
			m_world.m_terrain->SetTileSceneQueryFlags(1 << 6);

			// Merged quarter-page terrain rendering (draw-call batching). Client-only: the world
			// editor keeps the per-tile path so terrain editing works unchanged.
			if (const ConsoleVar* batchVar = ConsoleVarMgr::RegisterConsoleVar("gxTerrainBatching", "Whether terrain is rendered in merged quarter-page batches instead of one draw call per tile. Takes effect when terrain pages (re)load.", "1"))
			{
				m_world.m_terrain->SetBatchRenderingEnabled(batchVar->GetBoolValue());
			}
		}

		// Read terrain default material
		String defaultMaterialName;
		if (!(reader >> io::read_container<uint16>(defaultMaterialName)))
		{
			ELOG("Failed to read terrain default material name: Unexpected end of file");
			return false;
		}

		if (hasTerrain)
		{
			m_world.GetTerrain()->SetDefaultMaterial(MaterialManager::Get().Load(defaultMaterialName));
		}

		return reader;
	}
}
