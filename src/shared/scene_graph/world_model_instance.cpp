// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_instance.h"
#include "scene.h"
#include "scene_node.h"
#include "entity.h"
#include "camera.h"
#include "render_queue.h"
#include "mesh.h"
#include "mesh_manager.h"
#include "sub_mesh.h"
#include "material_manager.h"

namespace mmo
{
    const String WorldModelInstance::s_movableType = "WorldModelInstance";

    WorldModelInstance::WorldModelInstance(const String& name, WorldModelPtr worldModel)
        : MovableObject(name)
        , m_worldModel(std::move(worldModel))
        , m_activeDoodadSet(0)
        , m_boundingRadius(0.0f)
        , m_currentGroup(-1)
        , m_geometryCreated(false)
    {
        if (m_worldModel)
        {
            m_worldBoundingBox = m_worldModel->GetBoundingBox();
            m_boundingRadius = m_worldBoundingBox.GetExtents().GetLength();

            // Note: Geometry is created when attached to a scene node
            // in NotifyAttachmentChanged, because we need access to the Scene
        }
    }

    WorldModelInstance::~WorldModelInstance()
    {
        // Get scene from parent node if available
        Scene* scene = nullptr;
        if (m_parentNode)
        {
            auto* sceneNode = dynamic_cast<SceneNode*>(m_parentNode);
            if (sceneNode)
            {
                scene = &sceneNode->GetScene();
            }
        }

        ClearGeometry(scene);
        ClearDoodads(scene);
    }

    void WorldModelInstance::ClearGeometry(Scene* scene)
    {
        // Clean up entities and nodes
        for (auto& groupRenderable : m_groupRenderables)
        {
            for (auto& meshViz : groupRenderable.meshVisualizations)
            {
                if (meshViz.entity && meshViz.node)
                {
                    meshViz.node->DetachObject(*meshViz.entity);
                }
                
                if (scene)
                {
                    if (meshViz.entity)
                    {
                        scene->DestroyEntity(*meshViz.entity);
                    }
                    if (meshViz.node)
                    {
                        scene->DestroySceneNode(*meshViz.node);
                    }
                }
                
                meshViz.entity = nullptr;
                meshViz.node = nullptr;
            }
            groupRenderable.meshVisualizations.clear();
        }
        m_groupRenderables.clear();
        m_geometryCreated = false;
    }

    void WorldModelInstance::SetActiveDoodadSet(uint32 setIndex)
    {
        if (m_activeDoodadSet != setIndex)
        {
            m_activeDoodadSet = setIndex;
            
            // Re-create doodads if we have a scene (geometry was already created)
            if (m_geometryCreated && m_parentNode)
            {
                auto* sceneNode = dynamic_cast<SceneNode*>(m_parentNode);
                if (sceneNode)
                {
                    ClearDoodads(&sceneNode->GetScene());
                    CreateDoodads(sceneNode->GetScene());
                }
            }
        }
    }

    int32 WorldModelInstance::DetermineCurrentGroup(const Camera& camera) const
    {
        if (!m_worldModel)
        {
            return -1;
        }

        // Get camera position in world model local space
        Vector3 localCameraPos = camera.GetDerivedPosition();
        
        if (m_parentNode)
        {
            // Transform to local space
            const Matrix4 invWorld = m_parentNode->GetFullTransform().Inverse();
            localCameraPos = invWorld * localCameraPos;
        }

        // Check each group's bounding box
        // If multiple groups contain the camera, prefer the one with the smallest
        // bounding box volume (most specific/inner group)
        int32 bestGroupIndex = -1;
        float smallestVolume = std::numeric_limits<float>::max();

        for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
        {
            const auto* group = m_worldModel->GetGroup(i);
            if (group && group->GetBoundingBox().Intersects(localCameraPos))
            {
                const AABB& bbox = group->GetBoundingBox();
                Vector3 size = bbox.max - bbox.min;
                float volume = size.x * size.y * size.z;
                
                if (volume < smallestVolume)
                {
                    smallestVolume = volume;
                    bestGroupIndex = static_cast<int32>(i);
                }
            }
        }

        return bestGroupIndex;
    }

    bool WorldModelInstance::IsPointInside(const Vector3& point) const
    {
        if (!m_worldModel)
        {
            return false;
        }

        // Transform to local space
        Vector3 localPoint = point;
        
        if (m_parentNode)
        {
            const Matrix4 invWorld = m_parentNode->GetFullTransform().Inverse();
            localPoint = invWorld * point;
        }

        // Check against world model bounding box first
        if (!m_worldModel->GetBoundingBox().Intersects(localPoint))
        {
            return false;
        }

        // Check each group
        for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
        {
            const auto* group = m_worldModel->GetGroup(i);
            if (group && group->GetBoundingBox().Intersects(localPoint))
            {
                // For interior groups, we're "inside"
                if (group->IsInterior())
                {
                    return true;
                }
            }
        }

        return false;
    }

    uint32 WorldModelInstance::GetAmbientColorAtCamera(const Camera& camera) const
    {
        const int32 groupIndex = DetermineCurrentGroup(camera);
        
        if (groupIndex >= 0 && m_worldModel)
        {
            const auto* group = m_worldModel->GetGroup(groupIndex);
            if (group)
            {
                return group->GetAmbientColor();
            }
        }

        // Return world model default ambient
        return m_worldModel ? m_worldModel->GetAmbientColor() : 0xFFFFFFFF;
    }

    const String& WorldModelInstance::GetMovableType() const
    {
        return s_movableType;
    }

    const AABB& WorldModelInstance::GetBoundingBox() const
    {
        return m_worldBoundingBox;
    }

    float WorldModelInstance::GetBoundingRadius() const
    {
        return m_boundingRadius;
    }

    void WorldModelInstance::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
    {
        // Visit each visible group's renderables
        for (const auto& groupRenderable : m_groupRenderables)
        {
            for (const auto& meshViz : groupRenderable.meshVisualizations)
            {
                if (meshViz.entity)
                {
                    meshViz.entity->VisitRenderables(visitor, debugRenderables);
                }
            }
        }
    }

    void WorldModelInstance::PopulateRenderQueue(RenderQueue& renderQueue)
    {
        if (!m_worldModel || !IsVisible())
        {
            return;
        }

        // TODO: Implement portal culling here
        // For now, render all groups
        for (auto& groupRenderable : m_groupRenderables)
        {
            for (auto& meshViz : groupRenderable.meshVisualizations)
            {
                if (meshViz.entity)
                {
                    meshViz.entity->PopulateRenderQueue(renderQueue);
                }
            }
        }

        // Render doodads
        for (auto& doodad : m_doodadInstances)
        {
            if (doodad.entity)
            {
                doodad.entity->PopulateRenderQueue(renderQueue);
            }
        }
    }

    void WorldModelInstance::NotifyAttachmentChanged(Node* parent, bool isTagPoint)
    {
        MovableObject::NotifyAttachmentChanged(parent, isTagPoint);

        // Create geometry when attached to a scene node for the first time
        if (parent && !m_geometryCreated && m_worldModel)
        {
            auto* sceneNode = dynamic_cast<SceneNode*>(parent);
            if (sceneNode)
            {
                // Create renderables for each group
                for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
                {
                    CreateGroupGeometry(i, sceneNode->GetScene());
                }

                // Create doodads for the default set
                CreateDoodads(sceneNode->GetScene());

                m_geometryCreated = true;
            }
        }
    }

    void WorldModelInstance::PerformPortalCulling(const Camera& camera, int32 startGroupIndex, std::vector<int32>& visibleGroups) const
    {
        if (!m_worldModel || startGroupIndex < 0)
        {
            // If we're not in any group, render all exterior groups
            for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
            {
                const auto* group = m_worldModel->GetGroup(i);
                if (group && group->IsExterior())
                {
                    visibleGroups.push_back(static_cast<int32>(i));
                }
            }
            return;
        }

        // Start from the current group and cull through portals
        std::vector<bool> visitedGroups(m_worldModel->GetGroupCount(), false);
        CullThroughPortals(camera, startGroupIndex, visitedGroups, visibleGroups, 0);
    }

    void WorldModelInstance::CullThroughPortals(
        const Camera& camera,
        int32 currentGroupIndex,
        std::vector<bool>& visitedGroups,
        std::vector<int32>& visibleGroups,
        int32 recursionDepth) const
    {
        // Prevent infinite recursion
        if (recursionDepth > 16)
        {
            return;
        }

        // Mark this group as visited and visible
        if (currentGroupIndex < 0 || currentGroupIndex >= static_cast<int32>(visitedGroups.size()))
        {
            return;
        }

        if (visitedGroups[currentGroupIndex])
        {
            return;
        }

        visitedGroups[currentGroupIndex] = true;
        visibleGroups.push_back(currentGroupIndex);

        // Get the group's portal references
        const auto* group = m_worldModel->GetGroup(currentGroupIndex);
        if (!group)
        {
            return;
        }

        // For each portal in this group, check if it's visible and recurse
        for (const auto& portalRef : group->GetPortalRefs())
        {
            const int32 targetGroup = portalRef.groupIndex;
            
            if (targetGroup >= 0 && targetGroup < static_cast<int32>(visitedGroups.size()) && !visitedGroups[targetGroup])
            {
                // Check if portal is visible from camera
                // For now, we do a simple frustum check on the portal's bounding box
                if (portalRef.portalIndex < m_worldModel->GetPortals().size())
                {
                    const auto& portal = m_worldModel->GetPortals()[portalRef.portalIndex];
                    
                    if (portal && portal->IsActive())
                    {
                        // Simple visibility check - just check if target group bbox is in frustum
                        const auto* targetGroupData = m_worldModel->GetGroup(targetGroup);
                        if (targetGroupData)
                        {
                            // Transform group bbox to world space
                            AABB worldBBox = targetGroupData->GetBoundingBox();
                            if (m_parentNode)
                            {
                                worldBBox.Transform(m_parentNode->GetFullTransform());
                            }

                            if (camera.IsVisible(worldBBox))
                            {
                                CullThroughPortals(camera, targetGroup, visitedGroups, visibleGroups, recursionDepth + 1);
                            }
                        }
                    }
                }
            }
        }
    }

    void WorldModelInstance::CreateGroupGeometry(size_t groupIndex, Scene& scene)
    {
        if (!m_worldModel)
        {
            return;
        }

        const auto* group = m_worldModel->GetGroup(groupIndex);
        if (!group)
        {
            return;
        }

        GroupRenderable renderable;
        renderable.groupIndex = groupIndex;

        // Load and create entities for each mesh reference in this group
        const auto& meshRefs = group->GetMeshRefs();
        for (size_t i = 0; i < meshRefs.size(); ++i)
        {
            const auto& meshRef = meshRefs[i];
            if (!meshRef.visible)
            {
                continue;
            }

            MeshVisualization viz;

            // Load the mesh
            viz.mesh = MeshManager::Get().Load(meshRef.meshPath);
            if (!viz.mesh)
            {
                continue;
            }

            // Create a unique name for the entity
            const String nodeName = m_name + "_group" + std::to_string(groupIndex) + "_mesh" + std::to_string(i);
            
            // Create scene node under the parent
            viz.node = &scene.CreateSceneNode(nodeName);
            viz.node->SetPosition(meshRef.position);
            viz.node->SetOrientation(meshRef.rotation);
            viz.node->SetScale(meshRef.scale);
            
            // Attach to parent node
            if (m_parentNode)
            {
                static_cast<SceneNode*>(m_parentNode)->AddChild(*viz.node);
            }
            else
            {
                scene.GetRootSceneNode().AddChild(*viz.node);
            }

            // Create entity
            viz.entity = scene.CreateEntity(nodeName + "_entity", viz.mesh);
            if (viz.entity)
            {
                viz.entity->SetQueryFlags(GetQueryFlags());
                viz.node->AttachObject(*viz.entity);

                // Apply material override if specified
                if (!meshRef.materialOverride.empty())
                {
                    auto material = MaterialManager::Get().Load(meshRef.materialOverride);
                    if (material)
                    {
                        viz.entity->SetMaterial(material);
                    }
                }
            }

            renderable.meshVisualizations.push_back(std::move(viz));
        }

        m_groupRenderables.push_back(std::move(renderable));
    }

    void WorldModelInstance::CreateDoodads(Scene& scene)
    {
        if (!m_worldModel)
        {
            return;
        }

        const auto& doodadSets = m_worldModel->GetDoodadSets();
        const auto& doodads = m_worldModel->GetDoodads();
        const auto& doodadNames = m_worldModel->GetDoodadNames();

        // Always include set 0 (global set) plus the active set
        std::vector<std::pair<uint32, uint32>> setsToLoad;

        // Add global set if it exists
        if (!doodadSets.empty())
        {
            setsToLoad.push_back({doodadSets[0].startIndex, doodadSets[0].count});
        }

        // Add active set if different from global and valid
        if (m_activeDoodadSet > 0 && m_activeDoodadSet < doodadSets.size())
        {
            const auto& activeSet = doodadSets[m_activeDoodadSet];
            setsToLoad.push_back({activeSet.startIndex, activeSet.count});
        }

        // Create doodad instances
        size_t doodadCounter = 0;
        for (const auto& [startIndex, count] : setsToLoad)
        {
            for (uint32 i = 0; i < count; ++i)
            {
                const uint32 doodadIndex = startIndex + i;
                if (doodadIndex >= doodads.size())
                {
                    continue;
                }

                const auto& doodad = doodads[doodadIndex];
                if (doodad.nameIndex >= doodadNames.size())
                {
                    continue;
                }

                const String& meshPath = doodadNames[doodad.nameIndex];
                auto mesh = MeshManager::Get().Load(meshPath);
                if (!mesh)
                {
                    continue;
                }

                DoodadInstance instance;

                // Create scene node
                const String nodeName = m_name + "_doodad" + std::to_string(doodadCounter++);
                instance.node = &scene.CreateSceneNode(nodeName);
                instance.node->SetPosition(doodad.position);
                instance.node->SetOrientation(doodad.rotation);
                instance.node->SetScale(Vector3(doodad.scale, doodad.scale, doodad.scale));

                // Attach to parent node
                if (m_parentNode)
                {
                    static_cast<SceneNode*>(m_parentNode)->AddChild(*instance.node);
                }
                else
                {
                    scene.GetRootSceneNode().AddChild(*instance.node);
                }

                // Create entity
                instance.entity = scene.CreateEntity(nodeName + "_entity", mesh);
                if (instance.entity)
                {
                    instance.entity->SetQueryFlags(GetQueryFlags());
                    instance.node->AttachObject(*instance.entity);
                }
                
                m_doodadInstances.push_back(std::move(instance));
            }
        }
    }

    void WorldModelInstance::ClearDoodads(Scene* scene)
    {
        // Clean up entities and nodes
        for (auto& doodad : m_doodadInstances)
        {
            if (doodad.entity && doodad.node)
            {
                doodad.node->DetachObject(*doodad.entity);
            }
            
            if (scene)
            {
                if (doodad.entity)
                {
                    scene->DestroyEntity(*doodad.entity);
                }
                if (doodad.node)
                {
                    scene->DestroySceneNode(*doodad.node);
                }
            }
            
            doodad.entity = nullptr;
            doodad.node = nullptr;
        }
        m_doodadInstances.clear();
    }
}
