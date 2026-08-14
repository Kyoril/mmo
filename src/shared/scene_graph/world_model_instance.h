// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_model.h"
#include "instanced_mesh_collision.h"
#include "movable_object.h"
#include "world_model_batch.h"
#include "world_model_batch_builder.h"
#include "math/plane.h"

#include <map>
#include <memory>
#include <set>
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
    class Light;

    /// @brief Represents a frustum for portal culling, built from camera through portal vertices.
    /// This frustum narrows as we traverse through portals.
    struct PortalFrustum
    {
        /// @brief The planes that define this frustum (left, right, top, bottom from camera through portal).
        std::vector<Plane> planes;

        /// @brief The camera/eye position this frustum originates from.
        Vector3 origin;

        /// @brief Checks if a point is inside this portal frustum.
        /// @param point The point to test.
        /// @return True if the point is on the positive side of all planes.
        bool IsPointInside(const Vector3& point) const;

        /// @brief Checks if an AABB is visible within this portal frustum.
        /// @param bbox The bounding box to test.
        /// @return True if the box is at least partially visible.
        bool IsVisible(const AABB& bbox) const;

        /// @brief Checks if any of the portal vertices are visible within this frustum.
        /// @param vertices The portal vertices to test.
        /// @return True if at least one vertex is inside the frustum.
        bool IsPortalVisible(const std::vector<Vector3>& vertices) const;

        /// @brief Creates a new frustum narrowed through the given portal.
        /// @param portalVertices The 4 vertices of the portal in world space.
        /// @return A new PortalFrustum that is the intersection of this frustum and the portal.
        PortalFrustum ClipThroughPortal(const std::vector<Vector3>& portalVertices) const;

        /// @brief Creates an initial portal frustum from a camera.
        /// @param camera The camera to build the frustum from.
        /// @return A PortalFrustum representing the camera's view frustum.
        static PortalFrustum FromCamera(const Camera& camera);
    };

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

        /// @brief Destroys all scene objects created by this instance.
        /// Call this before destroying the instance to ensure proper cleanup.
        /// @param scene The scene to destroy objects in.
        void Destroy(Scene& scene);

        /// @brief Enables hardware-instanced batching of this instance's geometry.
        /// @details Modular world models repeat the same module mesh many times per room. With
        ///          batching on, every placement sharing a room, mesh, submesh and material is drawn
        ///          in one instanced draw call instead of one per placement, and collision moves to
        ///          dedicated proxies. Placements that cannot be batched (skinned meshes, meshes
        ///          without index data, materials with no compiled instanced shader variant, and
        ///          meshes placed only once) keep the ordinary per-entity path.
        ///
        ///          Off by default so both editors keep the per-entity path: the world editor picks
        ///          world models by hitting a child entity, and the transform gizmo needs those
        ///          entities to exist.
        /// @param enable Whether to batch. Must be set before the instance is attached to a scene
        ///        node - geometry is created on first attach, and this has no effect afterwards.
        void SetBatchingEnabled(const bool enable) { m_batchingEnabled = enable; }

        /// @brief Gets whether hardware-instanced batching is enabled. @see SetBatchingEnabled
        [[nodiscard]] bool IsBatchingEnabled() const { return m_batchingEnabled; }

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

        /// @copydoc MovableObject::NotifyMoved
        /// @details Batched instance matrices are world-space, so they have to be rebuilt whenever
        ///          the placement transform changes. This only flags them; the rebuild happens in
        ///          RefreshBatchTransforms during the next culling update.
        void NotifyMoved() override;

        /// @brief Updates portal culling visibility for all child entities.
        /// This should be called before the scene's FindVisibleObjects to ensure
        /// child entity visibility is set correctly before octree traversal.
        /// @param camera The camera to use for visibility calculations.
        void UpdatePortalCulling(Camera& camera);

    private:
        /// @brief Performs portal-based visibility culling.
        /// @param camera The camera to cull from.
        /// @param startGroupIndex The group to start culling from.
        /// @param visibleGroups Output vector of visible group indices.
        void PerformPortalCulling(const Camera& camera, int32 startGroupIndex, std::vector<int32>& visibleGroups) const;

        /// @brief Recursively culls through portals using frustum narrowing.
        /// @param frustum The current portal frustum (narrows as we traverse).
        /// @param currentGroupIndex Current group being processed.
        /// @param visitedGroups Set of already visited groups.
        /// @param visibleGroups Output vector of visible group indices.
        /// @param recursionDepth Current recursion depth.
        void CullThroughPortals(
            const PortalFrustum& frustum,
            int32 currentGroupIndex,
            std::vector<bool>& visitedGroups,
            std::vector<int32>& visibleGroups,
            int32 recursionDepth) const;

        /// @brief One instanced draw call plus the data needed to rebuild its world matrices.
        struct BatchEntry
        {
            WorldModelBatchPtr batch;

            /// @brief Node the batch hangs off. Always identity - see WorldModelBatch.
            SceneNode* node { nullptr };

            /// @brief Placements relative to the world model, composed against the placement node's
            ///        derived transform by ComposeWorldTransform.
            std::vector<WorldModelPlacementInput> placements;

            /// @brief Room this batch belongs to, or WorldModelNoGroup when it is never culled.
            size_t groupIndex { WorldModelNoGroup };
        };

        /// @brief Collision geometry for placements whose entities were replaced by a batch.
        struct CollisionEntry
        {
            /// @brief The collision proxy. Never gated by portal visibility.
            std::shared_ptr<InstancedMeshCollision> collision;

            /// @brief Node the proxy hangs off. Always identity, like the batch nodes.
            SceneNode* node { nullptr };

            /// @brief Mesh whose collision tree every instance shares.
            MeshPtr mesh;

            /// @brief Material override used to resolve surface types, or null for the mesh's own.
            MaterialPtr materialOverride;

            /// @brief Placements relative to the world model.
            std::vector<WorldModelPlacementInput> placements;
        };

        /// @brief Creates the renderable geometry for a group.
        /// @param groupIndex The group index to create geometry for.
        /// @param scene The scene to create entities in.
        void CreateGroupGeometry(size_t groupIndex, Scene& scene);

        /// @brief Creates one scene node plus entity for a single mesh reference.
        /// @details The per-placement path, shared by unbatched rendering and by placements that
        ///          batching had to demote.
        /// @param groupIndex Index of the group the reference belongs to.
        /// @param refIndex Index of the reference within that group.
        /// @param scene The scene to create the entity in.
        void CreateMeshRefEntity(size_t groupIndex, size_t refIndex, Scene& scene);

        /// @brief Builds instanced batches, demoted entities and collision proxies for all groups.
        /// @param scene The scene to create objects in.
        void BuildBatchedGeometry(Scene& scene);

        /// @brief Rebuilds and re-uploads instance matrices when the placement transform changed.
        /// @details Cheap no-op unless something marked the batches dirty. Must run on the main
        ///          thread, and never from PrepareRenderOperation or PreRender: Scene::RenderSingleObject
        ///          captures the render operation before PreRender runs, so touching GPU buffers there
        ///          would hand the draw a buffer that no longer matches.
        void RefreshBatchTransforms();

        /// @brief Destroys all batches and collision proxies.
        /// @param scene The scene to destroy objects in (can be null).
        void ClearBatches(Scene* scene);

        /// @brief Finds the innermost group containing a point in world model local space.
        /// @param localPoint The point to test, in world model local space.
        /// @return The group index, or -1 when the point lies in no group.
        [[nodiscard]] int32 FindGroupContaining(const Vector3& localPoint) const;

        /// @brief Whether a group is currently visible according to the last portal culling pass.
        [[nodiscard]] bool IsGroupVisible(int32 groupIndex) const;

        /// @brief Whether a mesh may be drawn through the instanced path at all.
        /// @param mesh The mesh to test.
        /// @return True when every submesh can be instanced.
        static bool CanBatchMesh(const Mesh& mesh);

        /// @brief Whether a material has a compiled instanced vertex shader variant.
        /// @details Without one the device warns once and draws nothing at all, so this is checked
        ///          up front and the affected placements fall back to entities.
        /// @param material The material to test.
        /// @return True when the material can be used for an instanced draw.
        static bool MaterialSupportsInstancing(const MaterialPtr& material);

        /// @brief Builds the mesh-facts lookup the bucketer uses, backed by the mesh manager.
        [[nodiscard]] WorldModelMeshLookup MakeMeshLookup();

        /// @brief Creates m_batchRootNode if it does not exist yet.
        /// @param scene The scene to create the node in.
        void EnsureBatchRootNode(Scene& scene);

        /// @brief Creates one batch renderable for a bucket and appends it to the given list.
        /// @param scene The scene to create the batch in.
        /// @param bucket The bucket to realize.
        /// @param target The list to append the resulting entry to.
        /// @return True when a batch was created; false when the bucket must fall back to per-placement
        ///         renderables and be excluded from the collision pass.
        bool CreateBatch(Scene& scene, const WorldModelBucket& bucket, std::vector<BatchEntry>& target);

        /// @brief Creates one collision proxy per mesh and material override across all buckets.
        /// @param scene The scene to create the proxies in.
        /// @param buckets The buckets whose placements need collision.
        /// @param target The list to append the resulting proxies to.
        void BuildCollisionProxies(Scene& scene, const std::vector<WorldModelBucket>& buckets, std::vector<CollisionEntry>& target);

        /// @brief Rebuilds the world transforms of one collision proxy list.
        /// @param entries The proxies to rebuild.
        /// @param parentPosition Derived position of the placement node.
        /// @param parentOrientation Derived orientation of the placement node.
        /// @param parentScale Derived scale of the placement node.
        void RefreshCollisionProxies(
            std::vector<CollisionEntry>& entries,
            const Vector3& parentPosition,
            const Quaternion& parentOrientation,
            const Vector3& parentScale);

        /// @brief Gets the placement node's derived transform, or identity when unattached.
        /// @param outPosition Receives the derived position.
        /// @param outOrientation Receives the derived orientation.
        /// @param outScale Receives the derived scale.
        void GetPlacementTransform(Vector3& outPosition, Quaternion& outOrientation, Vector3& outScale) const;

        /// @brief Destroys one collision proxy list.
        /// @param entries The proxies to destroy.
        /// @param scene The scene to destroy nodes in (can be null).
        void DestroyCollisionProxies(std::vector<CollisionEntry>& entries, Scene* scene);

        /// @brief Destroys one batch list.
        /// @param entries The batches to destroy.
        /// @param scene The scene to destroy nodes in (can be null).
        void DestroyBatches(std::vector<BatchEntry>& entries, Scene* scene);

        /// @brief Logs a one-off warning that a material cannot be instanced.
        /// @param materialName Name of the offending material.
        void WarnMissingInstancedVariant(const String& materialName);

        /// @brief Smallest number of placements worth turning into an instanced draw.
        /// @details One instanced draw of one instance is still one draw call, so a lone placement
        ///          gains nothing while costing an instance buffer, a dependency on the material's
        ///          instanced shader variant, and a collision proxy that has to reproduce what the
        ///          entity gave for free. Deliberately a constant rather than a cvar.
        static constexpr size_t kMinInstancesPerBatch = 2;

        /// @brief Mesh facts cache backing MakeMeshLookup for the lifetime of one build.
        std::map<String, WorldModelMeshFacts> m_meshFactsCache;

        /// @brief Source of unique collision proxy names across both proxy lists.
        size_t m_collisionProxyCounter { 0 };

        /// @brief Source of unique batch names across both batch lists. Shared rather than per-list,
        ///        so a mesh-ref batch and a doodad batch cannot end up with the same name.
        size_t m_batchNameCounter { 0 };

        /// @brief Creates doodad entities for the current doodad set.
        /// @param scene The scene to create entities in.
        void CreateDoodads(Scene& scene);

        /// @brief Creates lights from the world model.
        /// @param scene The scene to create lights in.
        void CreateLights(Scene& scene);

        /// @brief Clears all doodad entities.
        /// @param scene The scene to destroy entities in (can be null).
        void ClearDoodads(Scene* scene);

        /// @brief Clears all lights.
        /// @param scene The scene to destroy lights in (can be null).
        void ClearLights(Scene* scene);

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

            /// @brief Index of the room this doodad sits in, or -1 for "in no room".
            /// @details Derived by point-in-group testing at load time. WorldModelGroup::GetDoodadRefs()
            ///          exists but is never populated by the serializer or the editor, so it cannot be
            ///          used for this.
            int32 groupIndex { -1 };
        };
        std::vector<DoodadInstance> m_doodadInstances;

        std::vector<BatchEntry> m_batches;
        std::vector<BatchEntry> m_doodadBatches;
        std::vector<CollisionEntry> m_collisionProxies;

        /// @brief Collision for batched doodads. Separate from m_collisionProxies because changing
        ///        the active doodad set rebuilds the doodads only, and must not disturb the mesh
        ///        references' collision.
        std::vector<CollisionEntry> m_doodadCollisionProxies;

        /// @brief Parent of every batch and collision node. Never given a transform, because both
        ///        store world-space data that the scene graph must not transform a second time.
        SceneNode* m_batchRootNode { nullptr };

        bool m_batchingEnabled { false };
        bool m_batchesDirty { true };
        Matrix4 m_lastBatchTransform { Matrix4::Identity };

        /// @brief Materials already reported as lacking an instanced variant, so the warning is
        ///        logged once per material rather than once per batch.
        std::set<String> m_warnedInstancingMaterials;

        // Light instances
        struct LightInstance
        {
            Light* light { nullptr };
            SceneNode* node { nullptr };
        };
        std::vector<LightInstance> m_lightInstances;

        // Visibility state (updated each frame)
        mutable std::vector<int32> m_visibleGroups;
        mutable int32 m_currentGroup;

        static const String s_movableType;
    };

    typedef std::shared_ptr<WorldModelInstance> WorldModelInstancePtr;
}
