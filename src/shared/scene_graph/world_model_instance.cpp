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
    {
        if (m_worldModel)
        {
            m_worldBoundingBox = m_worldModel->GetBoundingBox();
            m_boundingRadius = m_worldBoundingBox.GetExtents().GetLength();

            // Create renderables for each group
            for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
            {
                CreateGroupGeometry(i);
            }

            // Create doodads for the default set
            CreateDoodads();
        }
    }

    WorldModelInstance::~WorldModelInstance()
    {
        ClearDoodads();
        m_groupRenderables.clear();
    }

    void WorldModelInstance::SetActiveDoodadSet(uint32 setIndex)
    {
        if (m_activeDoodadSet != setIndex)
        {
            m_activeDoodadSet = setIndex;
            ClearDoodads();
            CreateDoodads();
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
            if (groupRenderable.entity)
            {
                groupRenderable.entity->VisitRenderables(visitor, debugRenderables);
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
            if (groupRenderable.entity)
            {
                groupRenderable.entity->PopulateRenderQueue(renderQueue);
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

        // Update doodad node parents
        // Note: This may need to be implemented if we need doodads to be children of this node
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

    void WorldModelInstance::CreateGroupGeometry(size_t groupIndex)
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

        // Create a mesh for this group
        const String meshName = m_name + "_group_" + std::to_string(groupIndex);
        
        // For now, we'll create simple renderables
        // In a full implementation, we'd create proper Mesh objects from the group geometry
        // and use SubMesh with proper materials

        m_groupRenderables.push_back(std::move(renderable));
    }

    void WorldModelInstance::CreateDoodads()
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

                // In a full implementation, we would:
                // 1. Load the mesh from doodadNames[doodad.nameIndex]
                // 2. Create an Entity with that mesh
                // 3. Create a SceneNode and position it
                // 4. Apply scale and rotation from the doodad definition

                DoodadInstance instance;
                // instance.entity = ...
                // instance.node = ...
                
                m_doodadInstances.push_back(std::move(instance));
            }
        }
    }

    void WorldModelInstance::ClearDoodads()
    {
        m_doodadInstances.clear();
    }
}
