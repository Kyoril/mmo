// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/ray.h"

namespace mmo
{
    class Camera;
    class RaySceneQuery;
    class Selection;
    class ManualRenderObject;
    class WorldEditor;
    class SpawnEditMode;

    namespace terrain
    {
        class Terrain;
    }

    /// @brief Handles raycasting for object selection in the world editor.
    /// Performs raycasts against entities, spawns, and terrain to select objects.
    class SelectionRaycaster final : public NonCopyable
    {
    public:
        /// @brief Constructs the selection raycaster.
        /// @param camera Reference to the camera for viewport-to-world ray conversion.
        /// @param raySceneQuery Reference to the ray scene query for intersection tests.
        /// @param selection Reference to the selection manager.
        /// @param debugBoundingBox Reference to the debug bounding box visualization.
        /// @param terrain Pointer to the terrain (can be nullptr).
        /// @param editor Reference to the world editor for project access.
        /// @param spawnEditMode Pointer to the spawn edit mode (can be nullptr).
        explicit SelectionRaycaster(
            Camera &camera,
            RaySceneQuery &raySceneQuery,
            Selection &selection,
            ManualRenderObject &debugBoundingBox,
            terrain::Terrain *terrain,
            WorldEditor &editor,
            SpawnEditMode *spawnEditMode);

        ~SelectionRaycaster() override = default;

    public:
        /// @brief Performs a raycast to select entities.
        /// @param viewportX Normalized viewport X coordinate (0-1).
        /// @param viewportY Normalized viewport Y coordinate (0-1).
        /// @param allowMultiSelect Whether to allow multi-selection (Shift key held).
        void PerformEntitySelection(float viewportX, float viewportY, bool allowMultiSelect);

        /// @brief Performs a raycast to select spawns (units or objects).
        /// @param viewportX Normalized viewport X coordinate (0-1).
        /// @param viewportY Normalized viewport Y coordinate (0-1).
        void PerformSpawnSelection(float viewportX, float viewportY);

        /// @brief Performs a raycast to select terrain tiles.
        /// @param viewportX Normalized viewport X coordinate (0-1).
        /// @param viewportY Normalized viewport Y coordinate (0-1).
        void PerformTerrainSelection(float viewportX, float viewportY);

        /// @brief Sets the terrain reference.
        /// @param terrain Pointer to the terrain (can be nullptr).
        void SetTerrain(terrain::Terrain *terrain) { m_terrain = terrain; }

        /// @brief Sets the spawn edit mode reference.
        /// @param spawnEditMode Pointer to the spawn edit mode (can be nullptr).
        void SetSpawnEditMode(SpawnEditMode *spawnEditMode) { m_spawnEditMode = spawnEditMode; }

        /// @brief Updates the debug bounding box visualization.
        /// @param boundingBox The bounding box to visualize.
        void UpdateDebugAABB(const AABB &boundingBox);

    private:
        /// @brief Creates a ray from viewport coordinates.
        /// @param viewportX Normalized viewport X coordinate.
        /// @param viewportY Normalized viewport Y coordinate.
        /// @return The created ray.
        Ray CreateRayFromViewport(float viewportX, float viewportY) const;

        /// @brief Performs accurate ray-mesh intersection using triangle tests.
        /// @param ray The ray in world space.
        /// @param entity The entity to test against.
        /// @param outDistance The distance to the closest hit point (if any).
        /// @return True if the ray intersects the mesh geometry, false otherwise.
        bool IntersectMeshGeometry(const Ray &ray, class Entity *entity, float &outDistance) const;

    private:
        Camera &m_camera;
        RaySceneQuery &m_raySceneQuery;
        Selection &m_selection;
        ManualRenderObject &m_debugBoundingBox;
        terrain::Terrain *m_terrain;
        WorldEditor &m_editor;
        SpawnEditMode *m_spawnEditMode;
    };
}
