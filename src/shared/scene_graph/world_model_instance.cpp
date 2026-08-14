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
#include "log/default_log_levels.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace mmo
{
    namespace
    {
        /// @brief Converts a doodad's packed colour into a per-instance tint.
        /// @details WorldModelDoodad::color is documented BGRA, but the only writer in the editor
        ///          emits 0xFFFFFFFF, which is identical under every channel order - so the real
        ///          order is not observable from current content. Decoded as 0xAARRGGBB to match how
        ///          WorldModelLight::color is decoded in CreateLights. Revisit if tinted doodads are
        ///          ever authored.
        ///
        ///          Zero is treated as "unset" rather than transparent black: the per-entity path
        ///          ignores this field entirely today, so an unset value must not suddenly multiply
        ///          a dungeon's props down to nothing.
        Vector4 DoodadTint(const uint32 color)
        {
            if (color == 0 || color == 0xFFFFFFFF)
            {
                return Vector4(1.0f, 1.0f, 1.0f, 1.0f);
            }

            return Vector4(
                static_cast<float>((color >> 16) & 0xFF) / 255.0f,
                static_cast<float>((color >> 8) & 0xFF) / 255.0f,
                static_cast<float>((color >> 0) & 0xFF) / 255.0f,
                static_cast<float>((color >> 24) & 0xFF) / 255.0f);
        }
    }

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

        ClearBatches(scene);

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
        // Unregister from scene first, before clearing geometry resets m_geometryCreated
        // which would prevent unregistration in NotifyAttachmentChanged and the destructor
        scene.UnregisterWorldModelInstance(this);

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

        for (const auto& entry : m_batches)
        {
            if (entry.batch)
            {
                entry.batch->VisitRenderables(visitor, debugRenderables);
            }
        }

        for (const auto& entry : m_doodadBatches)
        {
            if (entry.batch)
            {
                entry.batch->VisitRenderables(visitor, debugRenderables);
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
        if (!m_worldModel)
        {
            return;
        }

        // Ahead of the visibility check on purpose. This is what populates the collision proxies with
        // their world transforms, and collision has to keep working while the world model is hidden -
        // scene collision queries filter on query flags, not on visibility.
        RefreshBatchTransforms();

        if (!IsVisible())
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
            const bool isVisible = IsGroupVisible(static_cast<int32>(i));

            auto& groupRenderable = m_groupRenderables[i];
            for (auto& meshViz : groupRenderable.meshVisualizations)
            {
                if (meshViz.entity)
                {
                    meshViz.entity->SetVisible(isVisible);
                }
            }
        }

        for (auto& entry : m_batches)
        {
            if (entry.batch)
            {
                entry.batch->SetVisible(IsGroupVisible(static_cast<int32>(entry.groupIndex)));
            }
        }

        // Doodads are culled by the room they were derived into. Until now they were never culled at
        // all - this loop only walked m_groupRenderables - so every doodad in a dungeon was submitted
        // whenever its bounding box was in frustum, including doodads in sealed-off rooms. Doodads
        // that lie in no room stay visible.
        for (auto& doodad : m_doodadInstances)
        {
            if (doodad.entity)
            {
                doodad.entity->SetVisible(doodad.groupIndex < 0 || IsGroupVisible(doodad.groupIndex));
            }
        }

        for (auto& entry : m_doodadBatches)
        {
            if (entry.batch)
            {
                entry.batch->SetVisible(entry.groupIndex == WorldModelNoGroup ||
                    IsGroupVisible(static_cast<int32>(entry.groupIndex)));
            }
        }

        // Collision proxies are deliberately NOT touched here. Collision must work regardless of what
        // the camera can see: the player stands on the floor of a room that portal culling has just
        // hidden, and camera collision queries run against geometry behind the camera.
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
                if (m_batchingEnabled)
                {
                    BuildBatchedGeometry(sceneNode->GetScene());
                }
                else
                {
                    for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
                    {
                        CreateGroupGeometry(i, sceneNode->GetScene());
                    }
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
        m_groupRenderables.push_back(std::move(renderable));

        // Load and create entities for each mesh reference in this group
        const auto& meshRefs = group->GetMeshRefs();
        for (size_t i = 0; i < meshRefs.size(); ++i)
        {
            if (!meshRefs[i].visible)
            {
                continue;
            }

            CreateMeshRefEntity(groupIndex, i, scene);
        }
    }

    void WorldModelInstance::CreateMeshRefEntity(const size_t groupIndex, const size_t refIndex, Scene& scene)
    {
        const auto* group = m_worldModel ? m_worldModel->GetGroup(groupIndex) : nullptr;
        if (!group || groupIndex >= m_groupRenderables.size())
        {
            return;
        }

        const auto* meshRef = group->GetMeshRef(refIndex);
        if (!meshRef)
        {
            return;
        }

        MeshVisualization viz;

        // Load the mesh
        viz.mesh = MeshManager::Get().Load(meshRef->meshPath);
        if (!viz.mesh)
        {
            return;
        }

        // Create a unique name for the entity
        const String nodeName = m_name + "_group" + std::to_string(groupIndex) + "_mesh" + std::to_string(refIndex);

        // Create scene node under the parent
        viz.node = &scene.CreateSceneNode(nodeName);
        viz.node->SetPosition(meshRef->position);
        viz.node->SetOrientation(meshRef->rotation);
        viz.node->SetScale(meshRef->scale);

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
            if (!meshRef->materialOverride.empty())
            {
                auto material = MaterialManager::Get().Load(meshRef->materialOverride);
                if (material)
                {
                    viz.entity->SetMaterial(material);
                }
            }
        }

        m_groupRenderables[groupIndex].meshVisualizations.push_back(std::move(viz));
    }

    WorldModelMeshLookup WorldModelInstance::MakeMeshLookup()
    {
        return [this](const String& meshPath) -> const WorldModelMeshFacts*
        {
            const auto cached = m_meshFactsCache.find(meshPath);
            if (cached != m_meshFactsCache.end())
            {
                return &cached->second;
            }

            const MeshPtr mesh = MeshManager::Get().Load(meshPath);
            if (!mesh)
            {
                // Reported as unresolvable; the bucketer sends the placement down the entity path,
                // where the existing load failure handling logs it.
                return nullptr;
            }

            WorldModelMeshFacts facts;
            facts.submeshCount = mesh->GetSubMeshCount();
            facts.batchable = CanBatchMesh(*mesh);

            return &m_meshFactsCache.emplace(meshPath, facts).first->second;
        };
    }

    void WorldModelInstance::EnsureBatchRootNode(Scene& scene)
    {
        if (m_batchRootNode)
        {
            return;
        }

        // Deliberately a child of the scene root rather than of this instance's placement node.
        // Batches and collision proxies both hold world-space data, and MovableObject::GetWorldBoundingBox
        // transforms local bounds by the parent's full transform - hanging them off the placement
        // node would apply the placement transform a second time.
        m_batchRootNode = &scene.CreateSceneNode(m_name + "_batches");
        scene.GetRootSceneNode().AddChild(*m_batchRootNode);
    }

    void WorldModelInstance::WarnMissingInstancedVariant(const String& materialName)
    {
        if (!m_warnedInstancingMaterials.insert(materialName).second)
        {
            return;
        }

        WLOG("World model '" << m_name << "': material '" << materialName << "' has no compiled instanced "
            "vertex shader variant - its geometry falls back to one draw call per placement. "
            "Recompile/resave the material to enable batching.");
    }

    void WorldModelInstance::CreateBatch(Scene& scene, const WorldModelBucket& bucket, std::vector<BatchEntry>& target)
    {
        const MeshPtr mesh = MeshManager::Get().Load(bucket.key.meshPath);
        if (!mesh || mesh->GetSubMeshCount() <= bucket.key.submeshIndex)
        {
            return;
        }

        MaterialPtr material;
        if (!bucket.key.materialOverride.empty())
        {
            material = MaterialManager::Get().Load(bucket.key.materialOverride);
        }
        if (!material)
        {
            material = mesh->GetSubMesh(bucket.key.submeshIndex).GetMaterial();
        }

        if (!material)
        {
            return;
        }

        const String batchName = m_name + "_batch" + std::to_string(target.size()) +
            "_g" + std::to_string(static_cast<int64>(bucket.key.groupIndex)) +
            "_s" + std::to_string(bucket.key.submeshIndex);

        BatchEntry entry;
        entry.batch = std::make_shared<WorldModelBatch>(batchName, mesh, bucket.key.submeshIndex, material);
        entry.groupIndex = bucket.key.groupIndex;
        entry.localTransforms.reserve(bucket.placements.size());
        entry.tints.reserve(bucket.placements.size());

        for (const auto& placement : bucket.placements)
        {
            entry.localTransforms.push_back(placement.localTransform);
            entry.tints.push_back(placement.tint);
        }

        // Upload before attaching, so the node's bounds are computed from real instance bounds the
        // first time round rather than from an empty batch. RefreshBatchTransforms re-uploads later
        // if the placement transform ever changes.
        const Matrix4 parentTransform = m_parentNode ? m_parentNode->GetFullTransform() : Matrix4::Identity;
        for (size_t i = 0; i < entry.localTransforms.size(); ++i)
        {
            MeshInstanceData instance;
            instance.worldMatrix = parentTransform * entry.localTransforms[i];
            instance.color = entry.tints[i];
            entry.batch->AddInstance(instance);
        }
        entry.batch->UploadInstances(GraphicsDevice::Get());

        entry.node = m_batchRootNode->CreateChildSceneNode();
        entry.node->AttachObject(*entry.batch);
        entry.batch->SetScene(&scene);

        // Batches carry no collision geometry and must never be picked; the proxies do that job.
        entry.batch->SetQueryFlags(0);

        target.push_back(std::move(entry));
    }

    void WorldModelInstance::BuildCollisionProxies(Scene& scene, const std::vector<WorldModelBucket>& buckets, std::vector<CollisionEntry>& target)
    {
        // One proxy per (mesh, material override) across the whole world model, not per room and not
        // per submesh: a proxy walks the mesh's collision tree once for all its instances, so a proxy
        // per submesh would report the same hit once per submesh.
        std::map<std::pair<String, String>, size_t> proxyIndices;

        for (const auto& bucket : buckets)
        {
            // Every submesh of a mesh produces its own bucket holding the same placements, so only
            // the first submesh contributes collision.
            if (bucket.key.submeshIndex != 0)
            {
                continue;
            }

            const MeshPtr mesh = MeshManager::Get().Load(bucket.key.meshPath);
            if (!mesh || mesh->GetCollisionTree().IsEmpty())
            {
                continue;
            }

            const auto proxyKey = std::make_pair(bucket.key.meshPath, bucket.key.materialOverride);
            auto it = proxyIndices.find(proxyKey);
            if (it == proxyIndices.end())
            {
                CollisionEntry entry;
                entry.mesh = mesh;
                if (!bucket.key.materialOverride.empty())
                {
                    entry.materialOverride = MaterialManager::Get().Load(bucket.key.materialOverride);
                }

                it = proxyIndices.emplace(proxyKey, target.size()).first;
                target.push_back(std::move(entry));
            }

            auto& proxy = target[it->second];
            for (const auto& placement : bucket.placements)
            {
                proxy.localTransforms.push_back(placement.localTransform);
            }
        }

        for (size_t i = 0; i < target.size(); ++i)
        {
            auto& entry = target[i];
            if (entry.collision)
            {
                continue;
            }

            entry.collision = std::make_shared<InstancedMeshCollision>(
                m_name + "_collision" + std::to_string(m_collisionProxyCounter++), entry.mesh);
            entry.collision->SetMaterialOverride(entry.materialOverride);

            // Matches what CreateMeshRefEntity gives its entities, so movement, the camera and
            // picking keep finding this geometry exactly as before.
            entry.collision->SetQueryFlags(GetQueryFlags());

            entry.node = m_batchRootNode->CreateChildSceneNode();
            entry.node->AttachObject(*entry.collision);
            entry.collision->SetScene(&scene);
        }
    }

    void WorldModelInstance::RefreshCollisionProxies(std::vector<CollisionEntry>& entries, const Matrix4& parentTransform)
    {
        for (auto& entry : entries)
        {
            if (!entry.node)
            {
                continue;
            }

            // InstancedMeshCollision accumulates instances and has no way to clear them, so it is
            // rebuilt from scratch. This only runs when the world model actually moves, which for
            // placed world models is never after load.
            const String name = entry.collision ? entry.collision->GetName() : m_name + "_collision";

            if (entry.collision)
            {
                entry.node->DetachObject(*entry.collision);
            }

            entry.collision = std::make_shared<InstancedMeshCollision>(name, entry.mesh);
            entry.collision->SetMaterialOverride(entry.materialOverride);
            entry.collision->SetQueryFlags(GetQueryFlags());

            for (const auto& localTransform : entry.localTransforms)
            {
                entry.collision->AddInstance(parentTransform * localTransform);
            }
            entry.collision->Finalize();

            entry.node->AttachObject(*entry.collision);
            entry.collision->SetScene(m_scene);
        }
    }

    void WorldModelInstance::RefreshBatchTransforms()
    {
        if (m_batches.empty() && m_doodadBatches.empty() &&
            m_collisionProxies.empty() && m_doodadCollisionProxies.empty())
        {
            return;
        }

        const Matrix4 parentTransform = m_parentNode ? m_parentNode->GetFullTransform() : Matrix4::Identity;
        if (!m_batchesDirty && parentTransform == m_lastBatchTransform)
        {
            return;
        }

        auto& device = GraphicsDevice::Get();

        const auto rebuild = [&device, &parentTransform](std::vector<BatchEntry>& entries)
        {
            for (auto& entry : entries)
            {
                if (!entry.batch)
                {
                    continue;
                }

                entry.batch->ClearInstances();
                for (size_t i = 0; i < entry.localTransforms.size(); ++i)
                {
                    MeshInstanceData instance;
                    instance.worldMatrix = parentTransform * entry.localTransforms[i];
                    instance.color = entry.tints[i];
                    entry.batch->AddInstance(instance);
                }

                entry.batch->UploadInstances(device);

                // The batch's bounds just changed, so the node that carries it has to recompute its
                // own world bounds and re-place itself in the octree.
                if (entry.node)
                {
                    entry.node->UpdateBounds();
                }
            }
        };

        rebuild(m_batches);
        rebuild(m_doodadBatches);

        RefreshCollisionProxies(m_collisionProxies, parentTransform);
        RefreshCollisionProxies(m_doodadCollisionProxies, parentTransform);

        m_lastBatchTransform = parentTransform;
        m_batchesDirty = false;
    }

    void WorldModelInstance::DestroyBatches(std::vector<BatchEntry>& entries, Scene* scene)
    {
        for (auto& entry : entries)
        {
            if (entry.batch && entry.node)
            {
                entry.node->DetachObject(*entry.batch);
            }

            if (scene && entry.node)
            {
                scene->DestroySceneNode(*entry.node);
            }

            entry.batch.reset();
            entry.node = nullptr;
        }
        entries.clear();
    }

    void WorldModelInstance::DestroyCollisionProxies(std::vector<CollisionEntry>& entries, Scene* scene)
    {
        for (auto& entry : entries)
        {
            if (entry.collision && entry.node)
            {
                entry.node->DetachObject(*entry.collision);
            }

            if (scene && entry.node)
            {
                scene->DestroySceneNode(*entry.node);
            }

            entry.collision.reset();
            entry.node = nullptr;
        }
        entries.clear();
    }

    void WorldModelInstance::ClearBatches(Scene* scene)
    {
        DestroyBatches(m_batches, scene);
        DestroyBatches(m_doodadBatches, scene);
        DestroyCollisionProxies(m_collisionProxies, scene);
        DestroyCollisionProxies(m_doodadCollisionProxies, scene);

        if (m_batchRootNode && scene)
        {
            scene->DestroySceneNode(*m_batchRootNode);
        }
        m_batchRootNode = nullptr;

        m_meshFactsCache.clear();
        m_batchesDirty = true;
    }

    void WorldModelInstance::NotifyMoved()
    {
        MovableObject::NotifyMoved();

        // Instance matrices are world-space, so a change to the placement transform invalidates them.
        m_batchesDirty = true;
    }

    bool WorldModelInstance::CanBatchMesh(const Mesh& mesh)
    {
        // Skinned geometry: GraphicsDevice picks the instanced vertex shader unconditionally once an
        // instance buffer is bound, so bone indices and bone matrices would simply be ignored and the
        // mesh would render in its bind pose at best.
        if (mesh.HasSkeleton())
        {
            return false;
        }

        if (mesh.GetSubMeshCount() == 0)
        {
            return false;
        }

        for (uint16 i = 0; i < mesh.GetSubMeshCount(); ++i)
        {
            // Not const: SubMesh::GetMaterial has no const overload.
            SubMesh& subMesh = mesh.GetSubMesh(i);

            // The instanced draw path only issues DrawIndexedInstanced; a submesh without index data
            // would silently render nothing.
            if (!subMesh.indexData || subMesh.indexData->indexCount == 0)
            {
                return false;
            }

            const VertexData* vertexData = subMesh.useSharedVertices ? mesh.sharedVertexData.get() : subMesh.vertexData.get();
            if (!vertexData || !vertexData->vertexDeclaration)
            {
                return false;
            }

            // HasSkeleton only checks that a skeleton name is set, so a mesh carrying blend data
            // without a resolvable skeleton would otherwise slip through the check above.
            if (vertexData->vertexDeclaration->FindElementBySemantic(VertexElementSemantic::BlendIndices))
            {
                return false;
            }

            if (!MaterialSupportsInstancing(subMesh.GetMaterial()))
            {
                return false;
            }
        }

        return true;
    }

    bool WorldModelInstance::MaterialSupportsInstancing(const MaterialPtr& material)
    {
        if (!material)
        {
            return false;
        }

        // Update() is what turns compiled bytecode into shader objects. It is guarded by dirty flags
        // internally, so calling it here is cheap and idempotent - and it has to happen now, because
        // waiting until Apply() would be far too late to pick a rendering path.
        material->Update();
        return material->GetVertexShader(VertexShaderType::Instanced) != nullptr;
    }

    int32 WorldModelInstance::FindGroupContaining(const Vector3& localPoint) const
    {
        if (!m_worldModel)
        {
            return -1;
        }

        // If several groups contain the point, prefer the one with the smallest bounding box, i.e.
        // the most specific room. Same rule DetermineCurrentGroup applies to the camera.
        int32 bestGroupIndex = -1;
        float smallestVolume = std::numeric_limits<float>::max();

        for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
        {
            const auto* group = m_worldModel->GetGroup(i);
            if (!group || !group->ContainsPoint(localPoint))
            {
                continue;
            }

            const AABB& bbox = group->GetBoundingBox();
            const Vector3 size = bbox.max - bbox.min;
            const float volume = size.x * size.y * size.z;

            if (volume < smallestVolume)
            {
                smallestVolume = volume;
                bestGroupIndex = static_cast<int32>(i);
            }
        }

        return bestGroupIndex;
    }

    bool WorldModelInstance::IsGroupVisible(const int32 groupIndex) const
    {
        return std::find(m_visibleGroups.begin(), m_visibleGroups.end(), groupIndex) != m_visibleGroups.end();
    }

    void WorldModelInstance::BuildBatchedGeometry(Scene& scene)
    {
        if (!m_worldModel)
        {
            return;
        }

        // One (initially empty) group renderable per group, so that group index stays usable as an
        // index into m_groupRenderables. Demoted placements land in these.
        for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
        {
            GroupRenderable renderable;
            renderable.groupIndex = i;
            m_groupRenderables.push_back(std::move(renderable));
        }

        // Collect every visible mesh reference as a placement.
        std::vector<WorldModelPlacementInput> placements;
        std::vector<WorldModelPlacementInput> ineligible;

        for (size_t groupIndex = 0; groupIndex < m_worldModel->GetGroupCount(); ++groupIndex)
        {
            const auto* group = m_worldModel->GetGroup(groupIndex);
            if (!group)
            {
                continue;
            }

            const auto& meshRefs = group->GetMeshRefs();
            for (size_t refIndex = 0; refIndex < meshRefs.size(); ++refIndex)
            {
                const auto& meshRef = meshRefs[refIndex];
                if (!meshRef.visible)
                {
                    continue;
                }

                WorldModelPlacementInput placement;
                placement.groupIndex = groupIndex;
                placement.sourceIndex = refIndex;
                placement.meshPath = meshRef.meshPath;
                placement.materialOverride = meshRef.materialOverride;
                placement.localTransform.MakeTransform(meshRef.position, meshRef.scale, meshRef.rotation);

                // An override replaces the material of every submesh, so it alone decides whether
                // this placement can be instanced. Checked here rather than in the bucketer, which
                // only knows mesh paths.
                if (!meshRef.materialOverride.empty() &&
                    !MaterialSupportsInstancing(MaterialManager::Get().Load(meshRef.materialOverride)))
                {
                    WarnMissingInstancedVariant(meshRef.materialOverride);
                    ineligible.push_back(std::move(placement));
                    continue;
                }

                placements.push_back(std::move(placement));
            }
        }

        std::vector<WorldModelBucket> buckets;
        std::vector<WorldModelPlacementInput> singletons;
        BuildWorldModelBuckets(placements, MakeMeshLookup(), kMinInstancesPerBatch, buckets, singletons);

        EnsureBatchRootNode(scene);

        for (const auto& bucket : buckets)
        {
            CreateBatch(scene, bucket, m_batches);
        }

        // Everything batching could not take renders exactly as it did before.
        for (const auto& placement : singletons)
        {
            CreateMeshRefEntity(placement.groupIndex, placement.sourceIndex, scene);
        }
        for (const auto& placement : ineligible)
        {
            CreateMeshRefEntity(placement.groupIndex, placement.sourceIndex, scene);
        }

        // Collision for batched placements only: demoted ones still have their entity, which carries
        // its own collision, and adding a proxy for them too would report every hit twice.
        BuildCollisionProxies(scene, buckets, m_collisionProxies);

        m_batchesDirty = true;
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

        // Gather the doodads of the active sets, deriving the room each one sits in.
        //
        // The room has to be derived by point-in-group testing: WorldModelGroup::GetDoodadRefs()
        // looks like it exists for exactly this, but nothing in the serializer or the editor ever
        // populates it, so it is always empty.
        std::vector<WorldModelPlacementInput> placements;

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

                const int32 groupIndex = FindGroupContaining(doodad.position);

                WorldModelPlacementInput placement;
                placement.groupIndex = groupIndex < 0 ? WorldModelNoGroup : static_cast<size_t>(groupIndex);
                placement.sourceIndex = doodadIndex;
                placement.meshPath = doodadNames[doodad.nameIndex];
                placement.localTransform.MakeTransform(
                    doodad.position,
                    Vector3(doodad.scale, doodad.scale, doodad.scale),
                    doodad.rotation);
                placement.tint = DoodadTint(doodad.color);

                placements.push_back(std::move(placement));
            }
        }

        std::vector<WorldModelPlacementInput> unbatched;

        if (m_batchingEnabled)
        {
            std::vector<WorldModelBucket> buckets;
            BuildWorldModelBuckets(placements, MakeMeshLookup(), kMinInstancesPerBatch, buckets, unbatched);

            EnsureBatchRootNode(scene);
            for (const auto& bucket : buckets)
            {
                CreateBatch(scene, bucket, m_doodadBatches);
            }

            BuildCollisionProxies(scene, buckets, m_doodadCollisionProxies);
            m_batchesDirty = true;
        }
        else
        {
            unbatched = std::move(placements);
        }

        // Anything not batched keeps the ordinary one-entity-per-doodad path.
        size_t doodadCounter = 0;
        for (const auto& placement : unbatched)
        {
            const auto& doodad = doodads[placement.sourceIndex];

            auto mesh = MeshManager::Get().Load(placement.meshPath);
            if (!mesh)
            {
                continue;
            }

            DoodadInstance instance;
            instance.groupIndex = placement.groupIndex == WorldModelNoGroup
                ? -1
                : static_cast<int32>(placement.groupIndex);

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

        // Batched doodads and their collision go too - changing the active doodad set rebuilds all
        // of it. The mesh references' batches and collision are deliberately left alone.
        DestroyBatches(m_doodadBatches, scene);
        DestroyCollisionProxies(m_doodadCollisionProxies, scene);
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
