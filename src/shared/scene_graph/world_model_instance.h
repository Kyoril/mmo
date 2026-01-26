// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_model.h"
#include "movable_object.h"

#include <memory>
#include <vector>

namespace mmo
{
    class Scene;
    class SceneNode;
    class Entity;
    class Camera;
    class RenderQueue;
    class SubMesh;
    class Mesh;

    /// @brief Represents a runtime instance of a world model placed in the scene.
    /// Handles rendering with portal culling, visibility determination, and collision.
    class WorldModelInstance : public MovableObject
    {
    public:
        /// @brief Creates a world model instance.
        /// @param name Unique name for this instance.
        /// @param worldModel The world model data to use.
        explicit WorldModelInstance(const String& name, WorldModelPtr worldModel);
        
        ~WorldModelInstance() override;

    public:
        /// @brief Gets the world model data.
        /// @return The world model.
        const WorldModelPtr& GetWorldModel() const { return m_worldModel; }

        /// @brief Sets the active doodad set.
        /// @param setIndex Index of the doodad set to activate.
        void SetActiveDoodadSet(uint32 setIndex);
        
        /// @brief Gets the active doodad set index.
        /// @return The active doodad set index.
        uint32 GetActiveDoodadSet() const { return m_activeDoodadSet; }

        /// @brief Determines which group the camera is currently inside.
        /// @param camera The camera to check.
        /// @return Index of the group the camera is in, or -1 if not in any group.
        int32 DetermineCurrentGroup(const Camera& camera) const;

        /// @brief Checks if a point is inside this world model.
        /// @param point The point to check in world space.
        /// @return True if the point is inside, false otherwise.
        bool IsPointInside(const Vector3& point) const;

        /// @brief Gets the ambient color for the current camera position.
        /// @param camera The camera to check.
        /// @return The ambient color as ARGB.
        uint32 GetAmbientColorAtCamera(const Camera& camera) const;

    public:
        // MovableObject overrides
        
        /// @copydoc MovableObject::GetMovableType
        const String& GetMovableType() const override;
        
        /// @copydoc MovableObject::GetBoundingBox
        const AABB& GetBoundingBox() const override;
        
        /// @copydoc MovableObject::GetBoundingRadius
        float GetBoundingRadius() const override;
        
        /// @copydoc MovableObject::VisitRenderables
        void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;
        
        /// @copydoc MovableObject::PopulateRenderQueue
        void PopulateRenderQueue(RenderQueue& renderQueue) override;

        /// @copydoc MovableObject::NotifyAttachmentChanged
        void NotifyAttachmentChanged(Node* parent, bool isTagPoint = false) override;

    private:
        /// @brief Performs portal-based visibility culling.
        /// @param camera The camera to cull from.
        /// @param startGroupIndex The group to start culling from.
        /// @param visibleGroups Output vector of visible group indices.
        void PerformPortalCulling(const Camera& camera, int32 startGroupIndex, std::vector<int32>& visibleGroups) const;

        /// @brief Recursively culls through portals.
        /// @param camera The camera to cull from.
        /// @param currentGroupIndex Current group being processed.
        /// @param visitedGroups Set of already visited groups.
        /// @param visibleGroups Output vector of visible group indices.
        /// @param recursionDepth Current recursion depth.
        void CullThroughPortals(
            const Camera& camera,
            int32 currentGroupIndex,
            std::vector<bool>& visitedGroups,
            std::vector<int32>& visibleGroups,
            int32 recursionDepth) const;

        /// @brief Creates the renderable geometry for a group.
        /// @param groupIndex The group index to create geometry for.
        /// @param scene The scene to create entities in.
        void CreateGroupGeometry(size_t groupIndex, Scene& scene);

        /// @brief Creates doodad entities for the current doodad set.
        /// @param scene The scene to create entities in.
        void CreateDoodads(Scene& scene);

        /// @brief Clears all doodad entities.
        /// @param scene The scene to destroy entities in (can be null).
        void ClearDoodads(Scene* scene);

        /// @brief Clears all geometry.
        /// @param scene The scene to destroy entities in (can be null).
        void ClearGeometry(Scene* scene);

    private:
        WorldModelPtr m_worldModel;
        uint32 m_activeDoodadSet;
        bool m_geometryCreated;
        
        AABB m_worldBoundingBox;
        float m_boundingRadius;
        
        // Mesh visualization within a group
        struct MeshVisualization
        {
            std::shared_ptr<Mesh> mesh;
            Entity* entity { nullptr };
            SceneNode* node { nullptr };
        };

        // Group renderables
        struct GroupRenderable
        {
            size_t groupIndex { 0 };
            std::vector<MeshVisualization> meshVisualizations;
        };
        std::vector<GroupRenderable> m_groupRenderables;
        
        // Doodad instances
        struct DoodadInstance
        {
            Entity* entity { nullptr };
            SceneNode* node { nullptr };
        };
        std::vector<DoodadInstance> m_doodadInstances;

        // Visibility state (updated each frame)
        mutable std::vector<int32> m_visibleGroups;
        mutable int32 m_currentGroup;

        static const String s_movableType;
    };

    typedef std::shared_ptr<WorldModelInstance> WorldModelInstancePtr;
}
