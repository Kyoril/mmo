// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "selection_raycaster.h"

#include "scene_graph/camera.h"
#include "scene_graph/scene.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "terrain/terrain.h"
#include "terrain/tile.h"
#include "terrain/page.h"
#include "selection.h"
#include "selected_map_entity.h"
#include "world_editor.h"
#include "edit_modes/spawn_edit_mode.h"

namespace mmo
{
    // Scene query flags for different object types
    static constexpr uint32 SceneQueryFlags_Entity = 1 << 0;
    static constexpr uint32 SceneQueryFlags_UnitSpawns = 1 << 2;
    static constexpr uint32 SceneQueryFlags_ObjectSpawns = 1 << 3;

    SelectionRaycaster::SelectionRaycaster(
        Camera &camera,
        RaySceneQuery &raySceneQuery,
        Selection &selection,
        ManualRenderObject &debugBoundingBox,
        terrain::Terrain *terrain,
        WorldEditor &editor,
        SpawnEditMode *spawnEditMode)
        : m_camera(camera), m_raySceneQuery(raySceneQuery), m_selection(selection), m_debugBoundingBox(debugBoundingBox), m_terrain(terrain), m_editor(editor), m_spawnEditMode(spawnEditMode)
    {
    }

    Ray SelectionRaycaster::CreateRayFromViewport(float viewportX, float viewportY) const
    {
        return m_camera.GetCameraToViewportRay(viewportX, viewportY, 10000.0f);
    }

    void SelectionRaycaster::UpdateDebugAABB(const AABB &aabb)
    {
        m_debugBoundingBox.Clear();

        auto lineListOp = m_debugBoundingBox.AddLineListOperation(MaterialManager::Get().Load("Models/Engine/WorldGrid.hmat"));

        lineListOp->AddLine(Vector3(aabb.min.x, aabb.min.y, aabb.min.z), Vector3(aabb.max.x, aabb.min.y, aabb.min.z));
        lineListOp->AddLine(Vector3(aabb.min.x, aabb.min.y, aabb.min.z), Vector3(aabb.min.x, aabb.max.y, aabb.min.z));
        lineListOp->AddLine(Vector3(aabb.min.x, aabb.min.y, aabb.min.z), Vector3(aabb.min.x, aabb.min.y, aabb.max.z));

        lineListOp->AddLine(Vector3(aabb.max.x, aabb.max.y, aabb.max.z), Vector3(aabb.min.x, aabb.max.y, aabb.max.z));
        lineListOp->AddLine(Vector3(aabb.max.x, aabb.max.y, aabb.max.z), Vector3(aabb.max.x, aabb.min.y, aabb.max.z));
        lineListOp->AddLine(Vector3(aabb.max.x, aabb.max.y, aabb.max.z), Vector3(aabb.max.x, aabb.max.y, aabb.min.z));

        lineListOp->AddLine(Vector3(aabb.max.x, aabb.min.y, aabb.min.z), Vector3(aabb.max.x, aabb.min.y, aabb.max.z));
        lineListOp->AddLine(Vector3(aabb.max.x, aabb.min.y, aabb.min.z), Vector3(aabb.max.x, aabb.max.y, aabb.min.z));

        lineListOp->AddLine(Vector3(aabb.min.x, aabb.max.y, aabb.min.z), Vector3(aabb.min.x, aabb.max.y, aabb.max.z));
        lineListOp->AddLine(Vector3(aabb.min.x, aabb.max.y, aabb.min.z), Vector3(aabb.max.x, aabb.max.y, aabb.min.z));

        lineListOp->AddLine(Vector3(aabb.min.x, aabb.min.y, aabb.max.z), Vector3(aabb.max.x, aabb.min.y, aabb.max.z));
        lineListOp->AddLine(Vector3(aabb.min.x, aabb.min.y, aabb.max.z), Vector3(aabb.min.x, aabb.max.y, aabb.max.z));
    }

    void SelectionRaycaster::PerformEntitySelection(float viewportX, float viewportY, bool allowMultiSelect)
    {
        const Ray ray = CreateRayFromViewport(viewportX, viewportY);
        m_raySceneQuery.SetRay(ray);
        m_raySceneQuery.SetSortByDistance(true);
        m_raySceneQuery.SetQueryMask(SceneQueryFlags_Entity);
        m_raySceneQuery.ClearResult();
        m_raySceneQuery.Execute();

        if (!allowMultiSelect)
        {
            m_selection.Clear();
        }

        m_debugBoundingBox.Clear();

        const auto &hitResult = m_raySceneQuery.GetLastResult();
        if (!hitResult.empty())
        {
            Entity *entity = (Entity *)hitResult[0].movable;
            if (entity)
            {
                MapEntity *mapEntity = entity->GetUserObject<MapEntity>();
                if (mapEntity)
                {
                    // Note: Duplication callback will be provided by WorldEditorInstance
                    // since it requires CreateMapEntity and GenerateUniqueId
                    m_selection.AddSelectable(std::make_unique<SelectedMapEntity>(*mapEntity, nullptr));
                    UpdateDebugAABB(hitResult[0].movable->GetWorldBoundingBox());
                }
            }
        }
    }

    void SelectionRaycaster::PerformSpawnSelection(float viewportX, float viewportY)
    {
        const Ray ray = CreateRayFromViewport(viewportX, viewportY);
        m_raySceneQuery.SetRay(ray);
        m_raySceneQuery.SetSortByDistance(true);
        m_raySceneQuery.SetQueryMask(SceneQueryFlags_UnitSpawns | SceneQueryFlags_ObjectSpawns);
        m_raySceneQuery.ClearResult();
        m_raySceneQuery.Execute();

        m_selection.Clear();
        m_debugBoundingBox.Clear();

        const auto &hitResult = m_raySceneQuery.GetLastResult();
        if (!hitResult.empty())
        {
            Entity *entity = (Entity *)hitResult[0].movable;
            if (entity)
            {
                if (entity->GetQueryFlags() & SceneQueryFlags_UnitSpawns)
                {
                    proto::UnitSpawnEntry *unitSpawnEntry = entity->GetUserObject<proto::UnitSpawnEntry>();
                    if (unitSpawnEntry)
                    {
                        // Duplication and deletion callbacks provided by WorldEditorInstance
                        m_selection.AddSelectable(std::make_unique<SelectedUnitSpawn>(
                            *unitSpawnEntry,
                            m_editor.GetProject().units,
                            m_editor.GetProject().models,
                            *entity->GetParentSceneNode()->GetParentSceneNode(),
                            *entity,
                            nullptr,   // Duplication not implemented
                            nullptr)); // Deletion will be set by WorldEditorInstance
                        UpdateDebugAABB(hitResult[0].movable->GetWorldBoundingBox());
                        return;
                    }
                }

                if (entity->GetQueryFlags() & SceneQueryFlags_ObjectSpawns)
                {
                    proto::ObjectSpawnEntry *objectSpawnEntry = entity->GetUserObject<proto::ObjectSpawnEntry>();
                    if (objectSpawnEntry)
                    {
                        m_selection.AddSelectable(std::make_unique<SelectedObjectSpawn>(
                            *objectSpawnEntry,
                            m_editor.GetProject().objects,
                            m_editor.GetProject().objectDisplays,
                            *entity->GetParentSceneNode()->GetParentSceneNode(),
                            *entity,
                            nullptr,   // Duplication not implemented
                            nullptr)); // Deletion will be set by WorldEditorInstance
                        UpdateDebugAABB(hitResult[0].movable->GetWorldBoundingBox());
                    }
                }
            }
        }
    }

    void SelectionRaycaster::PerformTerrainSelection(float viewportX, float viewportY)
    {
        if (!m_terrain)
        {
            return;
        }

        const Ray ray = CreateRayFromViewport(viewportX, viewportY);

        m_selection.Clear();
        m_debugBoundingBox.Clear();

        const auto hitResult = m_terrain->RayIntersects(ray);
        if (!hitResult.first)
        {
            return;
        }

        if (terrain::Tile *tile = hitResult.second.tile)
        {
            m_selection.AddSelectable(std::make_unique<SelectedTerrainTile>(*tile));
            UpdateDebugAABB(tile->GetPage().GetBoundingBox());
        }
    }
}
