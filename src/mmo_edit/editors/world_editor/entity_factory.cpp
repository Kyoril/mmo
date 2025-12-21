// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "entity_factory.h"

#include "scene_graph/scene.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "world_editor.h"
#include "world_editor_instance.h"
#include "scene_outline_window.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "terrain/terrain.h"
#include "log/default_log_levels.h"
#include "selection_raycaster.h"

namespace mmo
{
    EntityFactory::EntityFactory(
        Scene &scene,
        WorldEditor &editor,
        std::vector<std::unique_ptr<MapEntity>> &mapEntities,
        SceneOutlineWindow *sceneOutlineWindow)
        : m_scene(scene), m_editor(editor), m_mapEntities(mapEntities), m_sceneOutlineWindow(sceneOutlineWindow)
    {
    }

    uint64 EntityFactory::GenerateUniqueId()
    {
        // Get current time in milliseconds since epoch
        const auto now = std::chrono::system_clock::now();
        const uint64 timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        // Mix with random bits
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64> dis;
        const uint64 random = dis(gen) & 0x0000FFFFFFFFFFFF;

        // Format: 16 bits from timestamp + 48 bits random
        return (timestamp & 0xFFFF) << 48 | random;
    }

    void EntityFactory::NotifyExistingId(uint64 id)
    {
        m_objectIdGenerator.NotifyId(id);
    }

    Entity* EntityFactory::CreateMapEntity(const String &assetName, const Vector3 &position, const Quaternion &orientation, const Vector3 &scale, uint64 objectId)
    {
        if (objectId == 0)
        {
            objectId = GenerateUniqueId();
        }

        const String uniqueId = "Entity_" + std::to_string(objectId);

        // Entity already exists? This is an error!
        if (m_scene.HasEntity(uniqueId))
        {
            return m_scene.GetEntity(uniqueId);
        }

        Entity *entity = m_scene.CreateEntity(uniqueId, assetName);
        if (entity)
        {
            entity->SetQueryFlags(SceneQueryFlags_Entity);

            auto &node = m_scene.CreateSceneNode(uniqueId);
            m_scene.GetRootSceneNode().AddChild(node);
            node.AttachObject(*entity);
            node.SetPosition(position);
            node.SetOrientation(orientation);
            node.SetScale(scale);

            const auto &mapEntity = m_mapEntities.emplace_back(std::make_unique<MapEntity>(m_scene, node, *entity, objectId));
            mapEntity->SetReferencePagePosition(
                PagePosition(
                    static_cast<uint32>(floor(position.x / terrain::constants::PageSize)) + 32,
                    static_cast<uint32>(floor(position.z / terrain::constants::PageSize)) + 32));
            entity->SetUserObject(m_mapEntities.back().get());

            // Update scene outline when a new entity is created
            if (m_sceneOutlineWindow)
            {
                m_sceneOutlineWindow->Update();
            }
        }

        return entity;
    }

    Entity *EntityFactory::CreateUnitSpawnEntity(proto::UnitSpawnEntry &spawn)
    {
        proto::Project &project = m_editor.GetProject();

        // TODO: Use different mesh file?
        String meshFile = "Editor/Joint.hmsh";

        if (const auto *unit = project.units.getById(spawn.unitentry()))
        {
            const uint32 modelId = unit->malemodel() ? unit->malemodel() : unit->femalemodel();

            if (modelId == 0)
            {
                // TODO: Maybe spawn a dummy unit in editor later, so we can at least see, select and delete / modify the spawn
                WLOG("No model id assigned!");
            }
            else if (const auto *model = project.models.getById(modelId))
            {
                if (model->flags() & model_data_flags::IsCustomizable)
                {
                    auto definition = AvatarDefinitionManager::Get().Load(model->filename());
                    if (!definition)
                    {
                        ELOG("Unable to load avatar definition " << model->filename());
                    }
                    else
                    {
                        meshFile = definition->GetBaseMesh();

                        // TODO: Apply customization
                    }
                }
                else
                {
                    meshFile = model->filename();
                }
            }
            else
            {
                WLOG("Model " << modelId << " not found!");
            }
        }
        else
        {
            WLOG("Spawn point of non-existant unit " << spawn.unitentry() << " found");
        }

        const String uniqueId = "UnitSpawn_" + std::to_string(m_unitSpawnIdGenerator.GenerateId());
        Entity *entity = m_scene.CreateEntity(uniqueId, meshFile);
        if (entity)
        {
            ASSERT(entity->GetMesh());
            entity->SetQueryFlags(SceneQueryFlags_UnitSpawns);

            auto &node = m_scene.CreateSceneNode(uniqueId);
            m_scene.GetRootSceneNode().AddChild(node);
            node.AttachObject(*entity);
            node.SetPosition(Vector3(spawn.positionx(), spawn.positiony(), spawn.positionz()));
            node.SetOrientation(Quaternion(Radian(spawn.rotation()), Vector3::UnitY));
            node.SetScale(Vector3::UnitScale);

            // TODO: Is this safe? Does protobuf move the object around in memory?
            entity->SetUserObject(&spawn);
        }

        return entity;
    }

    Entity *EntityFactory::CreateObjectSpawnEntity(proto::ObjectSpawnEntry &spawn)
    {
        proto::Project &project = m_editor.GetProject();

        // TODO: Use different mesh file?
        String meshFile = "Editor/Joint.hmsh";

        if (const auto *object = project.objects.getById(spawn.objectentry()))
        {
            const uint32 modelId = object->displayid();

            if (modelId == 0)
            {
                // TODO: Maybe spawn a dummy unit in editor later, so we can at least see, select and delete / modify the spawn
                WLOG("No model id assigned!");
            }
            else if (const auto *model = project.objectDisplays.getById(modelId))
            {
                meshFile = model->filename();
            }
            else
            {
                WLOG("Model " << modelId << " not found!");
            }
        }
        else
        {
            WLOG("Spawn point of non-existant object " << spawn.objectentry() << " found");
        }

        const String uniqueId = "ObjectSpawn_" + std::to_string(m_objectSpawnIdGenerator.GenerateId());
        Entity *entity = m_scene.CreateEntity(uniqueId, meshFile);
        if (entity)
        {
            ASSERT(entity->GetMesh());
            entity->SetQueryFlags(SceneQueryFlags_ObjectSpawns);

            auto &node = m_scene.CreateSceneNode(uniqueId);
            m_scene.GetRootSceneNode().AddChild(node);
            node.AttachObject(*entity);

            node.SetPosition(Vector3(spawn.location().positionx(), spawn.location().positiony(), spawn.location().positionz()));
            node.SetOrientation(Quaternion(spawn.location().rotationw(), spawn.location().rotationx(), spawn.location().rotationy(), spawn.location().rotationz()));
            node.SetScale(Vector3::UnitScale);

            // TODO: Is this safe? Does protobuf move the object around in memory?
            entity->SetUserObject(&spawn);
        }

        return entity;
    }

    uint32 EntityFactory::GenerateUnitSpawnId()
    {
        return m_unitSpawnIdGenerator.GenerateId();
    }

    uint32 EntityFactory::GenerateObjectSpawnId()
    {
        return m_objectSpawnIdGenerator.GenerateId();
    }

    void EntityFactory::ResetUnitSpawnIdGenerator()
    {
        m_unitSpawnIdGenerator.Reset();
    }

    void EntityFactory::ResetObjectSpawnIdGenerator()
    {
        m_objectSpawnIdGenerator.Reset();
    }
}
