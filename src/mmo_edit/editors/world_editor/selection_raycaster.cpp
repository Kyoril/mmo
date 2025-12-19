// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "selection_raycaster.h"

#include "scene_graph/camera.h"
#include "scene_graph/scene.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/mesh.h"
#include "scene_graph/sub_mesh.h"
#include "graphics/vertex_index_data.h"
#include "graphics/vertex_declaration.h"
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

    bool SelectionRaycaster::IntersectMeshGeometry(const Ray &ray, Entity *entity, float &outDistance) const
    {
        if (!entity)
        {
            return false;
        }

        const MeshPtr &mesh = entity->GetMesh();
        if (!mesh)
        {
            return false;
        }

        // Get the entity's world transform
        SceneNode *parentNode = entity->GetParentSceneNode();
        if (!parentNode)
        {
            return false;
        }

        const Matrix4 &worldTransform = parentNode->GetFullTransform();
        const Matrix4 invWorldTransform = worldTransform.Inverse();

        // Transform ray to entity's local space
        const Vector3 localOrigin = invWorldTransform.TransformAffine(ray.origin);
        const Vector3 localDest = invWorldTransform.TransformAffine(ray.destination);
        const Ray localRay(localOrigin, localDest);

        float closestDistance = std::numeric_limits<float>::max();
        bool hitFound = false;

        // Iterate through all submeshes
        const uint16 subMeshCount = mesh->GetSubMeshCount();
        for (uint16 i = 0; i < subMeshCount; ++i)
        {
            SubMesh &subMesh = mesh->GetSubMesh(i);

            // Skip if no vertex or index data
            if (!subMesh.vertexData || !subMesh.indexData)
            {
                continue;
            }

            const VertexData *vertexData = subMesh.vertexData.get();
            const IndexData *indexData = subMesh.indexData.get();

            // Find position element
            const VertexElement *posElem = vertexData->vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Position);
            if (!posElem)
            {
                continue;
            }

            // Get vertex buffer for positions
            const VertexBufferBinding *binding = vertexData->vertexBufferBinding;
            const VertexBufferPtr &vertexBuffer = binding->GetBuffer(posElem->GetSource());
            if (!vertexBuffer)
            {
                continue;
            }

            // Lock vertex buffer for reading
            void *vertexDataPtr = vertexBuffer->Map(LockOptions::ReadOnly);
            if (!vertexDataPtr)
            {
                continue;
            }

            const uint32 vertexSize = vertexBuffer->GetVertexSize();
            const uint32 posOffset = posElem->GetOffset();

            // Get index buffer
            const IndexBufferPtr &indexBuffer = indexData->indexBuffer;
            if (!indexBuffer)
            {
                vertexBuffer->Unmap();
                continue;
            }

            void *indexDataPtr = indexBuffer->Map(LockOptions::ReadOnly);
            if (!indexDataPtr)
            {
                vertexBuffer->Unmap();
                continue;
            }

            const bool use32BitIndices = (indexBuffer->GetIndexSize() == IndexBufferSize::Index_32);
            const size_t indexCount = indexData->indexCount;

            // Test each triangle
            for (size_t triIdx = 0; triIdx < indexCount; triIdx += 3)
            {
                uint32 indices[3];

                // Read indices
                if (use32BitIndices)
                {
                    const uint32 *indexPtr = static_cast<const uint32 *>(indexDataPtr);
                    indices[0] = indexPtr[triIdx + 0];
                    indices[1] = indexPtr[triIdx + 1];
                    indices[2] = indexPtr[triIdx + 2];
                }
                else
                {
                    const uint16 *indexPtr = static_cast<const uint16 *>(indexDataPtr);
                    indices[0] = indexPtr[triIdx + 0];
                    indices[1] = indexPtr[triIdx + 1];
                    indices[2] = indexPtr[triIdx + 2];
                }

                // Read vertex positions
                Vector3 vertices[3];
                for (int v = 0; v < 3; ++v)
                {
                    const uint8 *vertexPtr = static_cast<const uint8 *>(vertexDataPtr) + (indices[v] * vertexSize) + posOffset;
                    const float *posPtr = reinterpret_cast<const float *>(vertexPtr);
                    vertices[v] = Vector3(posPtr[0], posPtr[1], posPtr[2]);
                }

                // Test ray against triangle
                const auto [hit, distance] = localRay.IntersectsTriangle(vertices[0], vertices[1], vertices[2]);
                if (hit && distance < closestDistance)
                {
                    closestDistance = distance;
                    hitFound = true;
                }
            }

            indexBuffer->Unmap();
            vertexBuffer->Unmap();
        }

        if (hitFound)
        {
            outDistance = closestDistance;
            return true;
        }

        return false;
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
            // Find the closest entity that actually intersects with the ray geometry
            Entity *closestEntity = nullptr;
            float closestDistance = std::numeric_limits<float>::max();

            for (const auto &result : hitResult)
            {
                Entity *entity = static_cast<Entity *>(result.movable);
                if (!entity)
                {
                    continue;
                }

                // Test against actual mesh geometry
                float hitDistance;
                if (IntersectMeshGeometry(ray, entity, hitDistance))
                {
                    if (hitDistance < closestDistance)
                    {
                        closestDistance = hitDistance;
                        closestEntity = entity;
                    }
                }
            }

            // Select the closest entity if we found one
            if (closestEntity)
            {
                MapEntity *mapEntity = closestEntity->GetUserObject<MapEntity>();
                if (mapEntity)
                {
                    // Note: Duplication callback will be provided by WorldEditorInstance
                    // since it requires CreateMapEntity and GenerateUniqueId
                    m_selection.AddSelectable(std::make_unique<SelectedMapEntity>(*mapEntity, nullptr));
                    UpdateDebugAABB(closestEntity->GetWorldBoundingBox());
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
