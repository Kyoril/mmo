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
#include "light.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
    // PortalFrustum implementation

    bool PortalFrustum::IsPointInside(const Vector3& point) const
    {
        // A point is inside if it's on the positive side of all planes
        for (const Plane& plane : planes)
        {
            if (plane.GetDistance(point) < 0)
            {
                return false;
            }
        }
        return true;
    }

    bool PortalFrustum::IsVisible(const AABB& bbox) const
    {
        if (bbox.IsNull())
        {
            return false;
        }

        const Vector3 center = bbox.GetCenter();
        const Vector3 halfSize = bbox.GetExtents();

        // Check against all planes - if the box is entirely behind any plane, it's not visible
        for (const Plane& plane : planes)
        {
            Plane::Side side = plane.GetSide(center, halfSize);
            if (side == Plane::NegativeSide)
            {
                return false;
            }
        }

        return true;
    }

    bool PortalFrustum::IsPortalVisible(const std::vector<Vector3>& vertices) const
    {
        if (vertices.empty())
        {
            return false;
        }

        // Check if all vertices are on the negative side of any single plane
        // If so, the portal is completely outside the frustum
        for (const Plane& plane : planes)
        {
            bool allOutside = true;
            for (const Vector3& v : vertices)
            {
                if (plane.GetDistance(v) >= 0)
                {
                    allOutside = false;
                    break;
                }
            }
            if (allOutside)
            {
                return false; // All vertices are behind this plane
            }
        }

        // At least one vertex is on the positive side of each plane,
        // so the portal is at least partially visible
        return true;
    }

    PortalFrustum PortalFrustum::ClipThroughPortal(const std::vector<Vector3>& portalVertices) const
    {
        if (portalVertices.size() < 3)
        {
            return *this;
        }

        PortalFrustum newFrustum;
        newFrustum.origin = origin;

        // Compute portal center for validating plane normal directions
        Vector3 portalCenter(0, 0, 0);
        for (const Vector3& v : portalVertices)
        {
            portalCenter += v;
        }
        portalCenter /= static_cast<float>(portalVertices.size());

        // Build planes from the eye/origin through each edge of the portal
        // Portal vertices are assumed to be in order (e.g., CCW or CW winding)
        const size_t numVerts = portalVertices.size();
        
        for (size_t i = 0; i < numVerts; ++i)
        {
            const Vector3& v0 = portalVertices[i];
            const Vector3& v1 = portalVertices[(i + 1) % numVerts];

            // Create a plane from origin through edge v0-v1
            Vector3 edge = v1 - v0;
            Vector3 toVertex = v0 - origin;
            
            // Cross product gives us the plane normal
            Vector3 planeNormal = edge.Cross(toVertex);
            
            float length = planeNormal.GetLength();
            if (length < 0.0001f)
            {
                continue; // Degenerate edge, skip
            }
            
            planeNormal /= length;
            
            // Ensure the normal points toward the portal center (inside the visible cone)
            // The portal center should be on the positive side of this plane
            Vector3 toCenter = portalCenter - origin;
            if (planeNormal.Dot(toCenter) < 0)
            {
                planeNormal = -planeNormal;
            }
            
            // The plane passes through the origin (camera position)
            Plane edgePlane(planeNormal, origin);
            newFrustum.planes.push_back(edgePlane);
        }

        // Also add the portal's own plane to ensure we don't see behind it
        // Calculate portal plane from first 3 vertices
        if (numVerts >= 3)
        {
            Vector3 portalNormal = (portalVertices[1] - portalVertices[0]).Cross(
                                   portalVertices[2] - portalVertices[0]);
            float length = portalNormal.GetLength();
            if (length > 0.0001f)
            {
                portalNormal /= length;
                
                // Make sure the portal plane faces toward the camera
                Vector3 toCamera = origin - portalVertices[0];
                if (portalNormal.Dot(toCamera) < 0)
                {
                    portalNormal = -portalNormal;
                }
                
                // Create plane - we want things on the far side of the portal to be visible
                // So the normal should point away from the camera (into the next room)
                Plane portalPlane(-portalNormal, portalVertices[0]);
                newFrustum.planes.push_back(portalPlane);
            }
        }

        // IMPORTANT: Add the parent frustum's NEW edge planes (not the original camera planes)
        // This creates the proper intersection of view cones through multiple portals.
        // We only want to keep planes that were created from portal edges, not the original
        // camera frustum planes (which are typically the first 6 planes).
        // For recursive portals, the parent will already have accumulated the necessary restrictions.
        for (const Plane& parentPlane : planes)
        {
            newFrustum.planes.push_back(parentPlane);
        }

        return newFrustum;
    }

    PortalFrustum PortalFrustum::FromCamera(const Camera& camera)
    {
        PortalFrustum frustum;
        frustum.origin = camera.GetDerivedPosition();
        
        // Extract the 6 frustum planes from the camera (skip far plane for infinite frustums)
        frustum.planes.resize(6);
        camera.ExtractFrustumPlanes(frustum.planes.data());
        
        return frustum;
    }

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

        // Unregister from scene before destruction
        if (scene)
        {
            scene->UnregisterWorldModelInstance(this);
        }

        ClearGeometry(scene);
        ClearDoodads(scene);
        ClearLights(scene);
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

        // Check each group using containment volumes (if defined) or AABB fallback
        // If multiple groups contain the camera, prefer the one with the smallest
        // bounding box volume (most specific/inner group)
        int32 bestGroupIndex = -1;
        float smallestVolume = std::numeric_limits<float>::max();

        for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
        {
            const auto* group = m_worldModel->GetGroup(i);
            if (group && group->ContainsPoint(localCameraPos))
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

        // Check each group using containment volumes (if defined) or AABB fallback
        for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
        {
            const auto* group = m_worldModel->GetGroup(i);
            if (group && group->ContainsPoint(localPoint))
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

    void WorldModelInstance::Destroy(Scene& scene)
    {
        // Clear all created scene objects
        ClearGeometry(&scene);
        ClearDoodads(&scene);
        ClearLights(&scene);
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

        // Entity visibility is now set by UpdatePortalCulling, which is called
        // by the Scene before FindVisibleObjects. We don't need to do anything here
        // as the entities render themselves through the normal octree traversal.
    }

    void WorldModelInstance::UpdatePortalCulling(Camera& camera)
    {
        if (!m_worldModel || !IsVisible())
        {
            return;
        }

        // Check if scene's culling is frozen - if so, use cached visibility
        Scene* scene = GetScene();
        const bool cullingFrozen = scene && scene->IsRenderingFrozen();

        if (!cullingFrozen)
        {
            // Update portal culling visibility
            m_visibleGroups.clear();

            // Determine current group from camera
            m_currentGroup = DetermineCurrentGroup(camera);
            PerformPortalCulling(camera, m_currentGroup, m_visibleGroups);
        }

        // Update entity visibility based on portal culling.
        // This is called BEFORE the octree traverses scene nodes, so the visibility
        // will be set correctly when entities are processed.
        for (size_t i = 0; i < m_groupRenderables.size(); ++i)
        {
            bool isVisible = std::find(m_visibleGroups.begin(), m_visibleGroups.end(), 
                static_cast<int32>(i)) != m_visibleGroups.end();
            
            auto& groupRenderable = m_groupRenderables[i];
            for (auto& meshViz : groupRenderable.meshVisualizations)
            {
                if (meshViz.entity)
                {
                    meshViz.entity->SetVisible(isVisible);
                }
            }
        }
    }

    void WorldModelInstance::NotifyAttachmentChanged(Node* parent, bool isTagPoint)
    {
        // Unregister from old scene if we had one
        if (m_parentNode && m_geometryCreated)
        {
            auto* oldSceneNode = dynamic_cast<SceneNode*>(m_parentNode);
            if (oldSceneNode)
            {
                oldSceneNode->GetScene().UnregisterWorldModelInstance(this);
            }
        }

        MovableObject::NotifyAttachmentChanged(parent, isTagPoint);

        // Create geometry when attached to a scene node for the first time
        if (parent && !m_geometryCreated && m_worldModel)
        {
            auto* sceneNode = dynamic_cast<SceneNode*>(parent);
            if (sceneNode)
            {
                // Set the scene reference so portal culling can access the active camera
                SetScene(&sceneNode->GetScene());

                // Register with scene for pre-render portal culling updates
                sceneNode->GetScene().RegisterWorldModelInstance(this);

                // Create renderables for each group
                for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
                {
                    CreateGroupGeometry(i, sceneNode->GetScene());
                }

                // Create doodads for the default set
                CreateDoodads(sceneNode->GetScene());

                // Create lights
                CreateLights(sceneNode->GetScene());

                m_geometryCreated = true;
            }
        }
        else if (parent && m_geometryCreated)
        {
            // Re-register with the new scene
            auto* sceneNode = dynamic_cast<SceneNode*>(parent);
            if (sceneNode)
            {
                SetScene(&sceneNode->GetScene());
                sceneNode->GetScene().RegisterWorldModelInstance(this);
            }
        }
    }

    void WorldModelInstance::PerformPortalCulling(const Camera& camera, int32 startGroupIndex, std::vector<int32>& visibleGroups) const
    {
        if (!m_worldModel)
        {
            return;
        }

        std::vector<bool> visitedGroups(m_worldModel->GetGroupCount(), false);

        // Create initial frustum from camera
        PortalFrustum cameraFrustum = PortalFrustum::FromCamera(camera);

        if (startGroupIndex < 0)
        {
            // Camera is not in any group - show all exterior groups and any
            // interior groups whose bounding box is visible in the camera frustum
            // Use frustum narrowing when traversing through portals
            
            struct CullEntry
            {
                int32 groupIndex;
                PortalFrustum frustum;
            };
            
            std::vector<CullEntry> groupsToProcess;
            
            for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
            {
                const auto* group = m_worldModel->GetGroup(i);
                if (group)
                {
                    // Always include exterior groups
                    if (group->IsExterior())
                    {
                        visibleGroups.push_back(static_cast<int32>(i));
                        visitedGroups[i] = true;
                        groupsToProcess.push_back({static_cast<int32>(i), cameraFrustum});
                    }
                    // Also include any group whose bounding box is visible in the frustum
                    else
                    {
                        AABB worldBBox = group->GetBoundingBox();
                        if (m_parentNode)
                        {
                            worldBBox.Transform(m_parentNode->GetFullTransform());
                        }
                        
                        if (camera.IsVisible(worldBBox))
                        {
                            visibleGroups.push_back(static_cast<int32>(i));
                            visitedGroups[i] = true;
                            groupsToProcess.push_back({static_cast<int32>(i), cameraFrustum});
                        }
                    }
                }
            }
            
            // Traverse through portals from visible groups using frustum narrowing
            while (!groupsToProcess.empty())
            {
                CullEntry entry = groupsToProcess.back();
                groupsToProcess.pop_back();

                if (entry.groupIndex < 0 || entry.groupIndex >= static_cast<int32>(visitedGroups.size()))
                {
                    continue;
                }

                const auto* group = m_worldModel->GetGroup(entry.groupIndex);
                if (!group)
                {
                    continue;
                }

                for (const auto& portalRef : group->GetPortalRefs())
                {
                    int32 targetGroup = portalRef.groupIndex;
                    if (targetGroup >= 0 && targetGroup < static_cast<int32>(visitedGroups.size()) && !visitedGroups[targetGroup])
                    {
                        if (portalRef.portalIndex < m_worldModel->GetPortals().size())
                        {
                            const auto& portal = m_worldModel->GetPortals()[portalRef.portalIndex];
                            if (portal && portal->IsActive())
                            {
                                // Get portal vertices in world space
                                std::vector<Vector3> portalVertices = portal->GetWorldVertices();
                                
                                // Transform portal vertices by the world model's transform
                                if (m_parentNode)
                                {
                                    const Matrix4& worldTransform = m_parentNode->GetFullTransform();
                                    for (Vector3& vertex : portalVertices)
                                    {
                                        vertex = worldTransform * vertex;
                                    }
                                }

                                // Check if portal is visible through the current frustum using vertex test
                                if (!portalVertices.empty() && entry.frustum.IsPortalVisible(portalVertices))
                                {
                                    visitedGroups[targetGroup] = true;
                                    visibleGroups.push_back(targetGroup);
                                    
                                    // Create a narrowed frustum through this portal
                                    PortalFrustum narrowedFrustum = entry.frustum.ClipThroughPortal(portalVertices);
                                    groupsToProcess.push_back({targetGroup, narrowedFrustum});
                                }
                            }
                        }
                    }
                }
            }
            return;
        }

        // Start from the current group and cull through portals using frustum narrowing
        CullThroughPortals(cameraFrustum, startGroupIndex, visitedGroups, visibleGroups, 0);
    }

    void WorldModelInstance::CullThroughPortals(
        const PortalFrustum& frustum,
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

        // For each portal in this group, check if it's visible through the current frustum and recurse
        for (const auto& portalRef : group->GetPortalRefs())
        {
            const int32 targetGroup = portalRef.groupIndex;
            
            if (targetGroup >= 0 && targetGroup < static_cast<int32>(visitedGroups.size()) && !visitedGroups[targetGroup])
            {
                // Check if portal is visible through the current (possibly narrowed) frustum
                if (portalRef.portalIndex < m_worldModel->GetPortals().size())
                {
                    const auto& portal = m_worldModel->GetPortals()[portalRef.portalIndex];
                    
                    if (portal && portal->IsActive())
                    {
                        // Get portal vertices in world space
                        std::vector<Vector3> portalVertices = portal->GetWorldVertices();
                        
                        // Transform portal vertices by the world model's transform
                        if (m_parentNode)
                        {
                            const Matrix4& worldTransform = m_parentNode->GetFullTransform();
                            for (Vector3& vertex : portalVertices)
                            {
                                vertex = worldTransform * vertex;
                            }
                        }

                        // Check if the portal is visible in the current frustum using vertex test
                        if (!portalVertices.empty() && frustum.IsPortalVisible(portalVertices))
                        {
                            // Create a narrowed frustum through this portal
                            PortalFrustum narrowedFrustum = frustum.ClipThroughPortal(portalVertices);
                            
                            // Recurse with the narrowed frustum
                            CullThroughPortals(narrowedFrustum, targetGroup, visitedGroups, visibleGroups, recursionDepth + 1);
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
            
            // Attach to parent node - this is needed for proper world transforms,
            // collision detection, and bounding box calculations
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

    void WorldModelInstance::CreateLights(Scene& scene)
    {
        ClearLights(&scene);

        if (!m_worldModel)
        {
            return;
        }

        const auto& lights = m_worldModel->GetLights();
        if (lights.empty())
        {
            return;
        }

        const String baseName = m_parentNode ? m_parentNode->GetName() : "wmo";
        uint32 lightIndex = 0;

        for (const auto& wmoLight : lights)
        {
            // Skip ambient lights - they don't map to scene lights
            if (wmoLight.type == WorldModelLight::LightType::Ambient)
            {
                continue;
            }

            // Map WMO light type to scene light type
            LightType sceneType = LightType::Point;
            switch (wmoLight.type)
            {
            case WorldModelLight::LightType::Omni:
                sceneType = LightType::Point;
                break;
            case WorldModelLight::LightType::Spot:
                sceneType = LightType::Spot;
                break;
            case WorldModelLight::LightType::Direct:
                sceneType = LightType::Directional;
                break;
            default:
                break;
            }

            // Create a unique name for this light
            String lightName = baseName + "_light_" + std::to_string(lightIndex++);

            // Create scene node for the light
            SceneNode& lightNode = scene.CreateSceneNode(lightName + "_node");

            // Attach to parent node
            if (m_parentNode)
            {
                static_cast<SceneNode*>(m_parentNode)->AddChild(lightNode);
            }
            else
            {
                scene.GetRootSceneNode().AddChild(lightNode);
            }

            // Set light position relative to parent
            lightNode.SetPosition(wmoLight.position);
            
            // Only set orientation for spot/directional lights that actually use it
            // Validate quaternion before using to avoid NaN issues
            if (sceneType != LightType::Point)
            {
                Quaternion rotation = wmoLight.rotation;
                const float lengthSq = rotation.x * rotation.x + rotation.y * rotation.y + 
                                       rotation.z * rotation.z + rotation.w * rotation.w;
                if (lengthSq > 0.001f)
                {
                    rotation.Normalize();
                    lightNode.SetOrientation(rotation);
                }
            }

            // Create the light
            Light& light = scene.CreateLight(lightName, sceneType);

            // Convert ARGB color to Vector4 (RGBA, normalized)
            // Format: 0xAARRGGBB
            const float r = static_cast<float>((wmoLight.color >> 16) & 0xFF) / 255.0f;
            const float g = static_cast<float>((wmoLight.color >> 8) & 0xFF) / 255.0f;
            const float b = static_cast<float>((wmoLight.color >> 0) & 0xFF) / 255.0f;
            light.SetColor(Vector4(r, g, b, 1.0f));

            // Set intensity - clamp to reasonable range to avoid over-brightening
            const float clampedIntensity = std::min(wmoLight.intensity, 10.0f);
            light.SetIntensity(clampedIntensity);

            // Set range from attenuation end
            if (wmoLight.useAttenuation && wmoLight.attenuationEnd > 0.0f)
            {
                light.SetRange(wmoLight.attenuationEnd);
            }
            else
            {
                // Default range if no attenuation
                light.SetRange(50.0f);
            }

            // Attach light to node
            lightNode.AttachObject(light);

            // Store the light instance
            LightInstance instance;
            instance.light = &light;
            instance.node = &lightNode;
            m_lightInstances.push_back(std::move(instance));
        }
    }

    void WorldModelInstance::ClearLights(Scene* scene)
    {
        for (auto& lightInstance : m_lightInstances)
        {
            if (lightInstance.light && lightInstance.node)
            {
                lightInstance.node->DetachObject(*lightInstance.light);
            }

            if (scene)
            {
                if (lightInstance.light)
                {
                    scene->DestroyLight(*lightInstance.light);
                }
                if (lightInstance.node)
                {
                    scene->DestroySceneNode(*lightInstance.node);
                }
            }

            lightInstance.light = nullptr;
            lightInstance.node = nullptr;
        }
        m_lightInstances.clear();
    }
}
