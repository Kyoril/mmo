// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"
#include "math/vector3.h"
#include "math/quaternion.h"
#include "math/aabb.h"
#include "math/plane.h"
#include "scene_graph/portal.h"

#include <vector>
#include <memory>
#include <string>
#include <map>

namespace mmo
{
    class Scene;
    class SceneNode;
    class Entity;
    class Camera;
    class RenderQueue;
    class Material;
    
    /// @brief Flags for world model groups.
    namespace WorldModelGroupFlags
    {
        enum Type : uint32
        {
            None = 0,
            
            /// @brief This group is an exterior/outdoor group.
            Exterior = 0x0008,
            
            /// @brief This group has vertex colors.
            HasVertexColors = 0x0004,
            
            /// @brief This group has lights.
            HasLights = 0x0200,
            
            /// @brief This group has doodads (props/static meshes placed within).
            HasDoodads = 0x0800,
            
            /// @brief This group has liquid/water.
            HasLiquid = 0x1000,
            
            /// @brief This group is an interior/indoor group.
            Interior = 0x2000,
            
            /// @brief This group always renders (ignores portal culling).
            AlwaysDraw = 0x10000,
            
            /// @brief Show skybox when inside this group.
            ShowSkybox = 0x40000,
        };
    }

    /// @brief Represents a doodad (static mesh/prop) placed within a world model group.
    struct WorldModelDoodad
    {
        /// @brief Index into the doodad name list.
        uint32 nameIndex;
        
        /// @brief Position relative to the world model.
        Vector3 position;
        
        /// @brief Rotation as quaternion.
        Quaternion rotation;
        
        /// @brief Uniform scale factor.
        float scale;
        
        /// @brief Tint color (BGRA).
        uint32 color;
    };

    /// @brief Represents a set of doodads that can be toggled on/off.
    struct WorldModelDoodadSet
    {
        /// @brief Name of the doodad set.
        String name;
        
        /// @brief Index of the first doodad in this set.
        uint32 startIndex;
        
        /// @brief Number of doodads in this set.
        uint32 count;
    };

    /// @brief Represents a light source within a world model.
    struct WorldModelLight
    {
        enum class LightType : uint8
        {
            Omni = 0,       ///< Point light
            Spot = 1,       ///< Spotlight
            Direct = 2,     ///< Directional light
            Ambient = 3     ///< Ambient light
        };

        /// @brief Type of light.
        LightType type;
        
        /// @brief Whether attenuation is used.
        bool useAttenuation;
        
        /// @brief Light color (BGRA).
        uint32 color;
        
        /// @brief Position in world model space.
        Vector3 position;
        
        /// @brief Light intensity.
        float intensity;
        
        /// @brief Rotation for spot/directional lights.
        Quaternion rotation;
        
        /// @brief Attenuation start distance.
        float attenuationStart;
        
        /// @brief Attenuation end distance.
        float attenuationEnd;
    };

    /// @brief Represents fog settings within a world model.
    struct WorldModelFog
    {
        /// @brief Fog position.
        Vector3 position;
        
        /// @brief Inner radius (start of fog).
        float innerRadius;
        
        /// @brief Outer radius (full fog).
        float outerRadius;
        
        /// @brief Fog end distance.
        float fogEnd;
        
        /// @brief Fog start multiplier (0..1).
        float fogStartScalar;
        
        /// @brief Fog color.
        uint32 fogColor;
        
        /// @brief Underwater fog end distance.
        float underwaterFogEnd;
        
        /// @brief Underwater fog start scalar.
        float underwaterFogStartScalar;
        
        /// @brief Underwater fog color.
        uint32 underwaterFogColor;
    };

    /// @brief Represents a mesh reference within a world model group.
    /// This allows a group to contain multiple meshes, each with their own
    /// transform and material override.
    struct WorldModelMeshRef
    {
        /// @brief Path to the mesh file (.hmsh).
        String meshPath;
        
        /// @brief Local position within the group.
        Vector3 position { 0.0f, 0.0f, 0.0f };
        
        /// @brief Local rotation within the group.
        Quaternion rotation { Quaternion::Identity };
        
        /// @brief Local scale.
        Vector3 scale { 1.0f, 1.0f, 1.0f };
        
        /// @brief Optional material override. Empty means use mesh's default material.
        String materialOverride;
        
        /// @brief Whether this mesh reference is visible.
        bool visible { true };
        
        /// @brief User-defined name for this mesh reference (for editor display).
        String name;
    };

    /// @brief Represents a child world model reference.
    /// This allows nesting world models within other world models,
    /// such as building a town WMO from multiple house WMOs.
    struct WorldModelChildRef
    {
        /// @brief Path to the child world model file (.hwmo).
        String wmoPath;
        
        /// @brief Local position within the parent.
        Vector3 position { 0.0f, 0.0f, 0.0f };
        
        /// @brief Local rotation within the parent.
        Quaternion rotation { Quaternion::Identity };
        
        /// @brief Local scale.
        Vector3 scale { 1.0f, 1.0f, 1.0f };
        
        /// @brief Whether this child reference is visible.
        bool visible { true };
        
        /// @brief User-defined name for this child reference (for editor display).
        String name;
    };

    /// @brief Represents a convex containment volume for accurate inside/outside detection.
    /// 
    /// Containment volumes provide more accurate point-in-group testing than simple AABBs.
    /// For complex room shapes (L-shaped, T-shaped, etc.), multiple containment volumes
    /// can be used to define the exact interior space without the AABB extending outside
    /// the actual geometry.
    /// 
    /// A point is considered inside the volume if it is on the negative side (behind)
    /// all planes in the volume.
    struct ContainmentVolume
    {
        /// @brief Convex hull planes defining this volume.
        /// A point is inside if it's behind (negative distance) ALL planes.
        std::vector<Plane> planes;
        
        /// @brief Quick-reject bounding box for this volume.
        AABB boundingBox;
        
        /// @brief User-friendly name for editor display.
        String name;
        
        /// @brief Check if a point is inside this convex volume.
        /// @param point The point to test.
        /// @return True if the point is inside (behind all planes), false otherwise.
        bool ContainsPoint(const Vector3& point) const
        {
            // Quick AABB rejection test
            if (!boundingBox.Intersects(point))
            {
                return false;
            }
            
            // Check all planes - point must be behind (negative side) of all
            for (const auto& plane : planes)
            {
                if (plane.GetDistance(point) > 0.0001f)
                {
                    return false;
                }
            }
            return true;
        }
        
        /// @brief Creates a box-shaped containment volume from an AABB.
        /// @param box The AABB to create the volume from.
        /// @param volumeName Optional name for the volume.
        /// @return A containment volume with 6 planes defining the box.
        static ContainmentVolume FromAABB(const AABB& box, const String& volumeName = "")
        {
            ContainmentVolume volume;
            volume.boundingBox = box;
            volume.name = volumeName;
            
            // Create 6 planes facing inward (normals point outward, so points inside are behind all planes)
            // Left plane (-X)
            volume.planes.emplace_back(Vector3(-1, 0, 0), box.min);
            // Right plane (+X)  
            volume.planes.emplace_back(Vector3(1, 0, 0), box.max);
            // Bottom plane (-Y)
            volume.planes.emplace_back(Vector3(0, -1, 0), box.min);
            // Top plane (+Y)
            volume.planes.emplace_back(Vector3(0, 1, 0), box.max);
            // Back plane (-Z)
            volume.planes.emplace_back(Vector3(0, 0, -1), box.min);
            // Front plane (+Z)
            volume.planes.emplace_back(Vector3(0, 0, 1), box.max);
            
            return volume;
        }
        
        /// @brief Recalculates the bounding box from the planes.
        /// Note: This is an approximation based on plane intersections.
        void RecalculateBoundingBox()
        {
            // For box volumes created from AABB, this is already accurate.
            // For more complex volumes, we'd need to compute plane intersections.
            // For now, we keep the existing bounding box or set a null one if empty.
            if (planes.empty())
            {
                boundingBox.SetNull();
            }
        }
    };

    /// @brief Represents a portal reference linking groups.
    struct WorldModelPortalRef
    {
        /// @brief Index of the portal in the portal list.
        uint16 portalIndex;
        
        /// @brief Index of the group this portal leads to.
        uint16 groupIndex;
        
        /// @brief Side of the portal (positive or negative).
        int16 side;
    };

    /// @brief Information about a single group in the world model.
    struct WorldModelGroupInfo
    {
        /// @brief Group flags.
        uint32 flags;
        
        /// @brief Bounding box of this group.
        AABB boundingBox;
        
        /// @brief Offset to group name in name list (-1 for no name).
        int32 nameOffset;
    };

    /// @brief Represents a single group (room/section) within a world model.
    /// Groups are the fundamental building blocks of world models. Each group can be
    /// interior (indoor) or exterior (outdoor) and is connected to other groups via portals.
    class WorldModelGroup
    {
    public:
        WorldModelGroup();
        ~WorldModelGroup();

    public:
        /// @brief Gets the group name.
        /// @return The group name.
        const String& GetName() const { return m_name; }
        
        /// @brief Sets the group name.
        /// @param name The new group name.
        void SetName(const String& name) { m_name = name; }
        
        /// @brief Gets the group flags.
        /// @return The group flags.
        uint32 GetFlags() const { return m_flags; }
        
        /// @brief Sets the group flags.
        /// @param flags The new group flags.
        void SetFlags(uint32 flags) { m_flags = flags; }
        
        /// @brief Checks if this group is an interior group.
        /// @return True if interior, false if exterior.
        bool IsInterior() const { return (m_flags & WorldModelGroupFlags::Interior) != 0; }
        
        /// @brief Checks if this group is an exterior group.
        /// @return True if exterior.
        bool IsExterior() const { return (m_flags & WorldModelGroupFlags::Exterior) != 0; }
        
        /// @brief Gets the bounding box of this group.
        /// @return The bounding box.
        const AABB& GetBoundingBox() const { return m_boundingBox; }
        
        /// @brief Sets the bounding box of this group.
        /// @param box The new bounding box.
        void SetBoundingBox(const AABB& box) { m_boundingBox = box; }
        
        /// @brief Gets the ambient color for this group.
        /// @return The ambient color.
        uint32 GetAmbientColor() const { return m_ambientColor; }
        
        /// @brief Sets the ambient color for this group.
        /// @param color The new ambient color.
        void SetAmbientColor(uint32 color) { m_ambientColor = color; }

        /// @brief Gets portal references for this group.
        const std::vector<WorldModelPortalRef>& GetPortalRefs() const { return m_portalRefs; }
        
        /// @brief Gets mutable portal references.
        std::vector<WorldModelPortalRef>& GetPortalRefs() { return m_portalRefs; }

        /// @brief Gets the list of light indices used by this group.
        const std::vector<uint16>& GetLightRefs() const { return m_lightRefs; }
        
        /// @brief Gets mutable light references.
        std::vector<uint16>& GetLightRefs() { return m_lightRefs; }

        /// @brief Gets the list of doodad indices used by this group.
        const std::vector<uint16>& GetDoodadRefs() const { return m_doodadRefs; }
        
        /// @brief Gets mutable doodad references.
        std::vector<uint16>& GetDoodadRefs() { return m_doodadRefs; }

        /// @brief Gets the mesh references for this group.
        /// @return Const reference to mesh references vector.
        const std::vector<WorldModelMeshRef>& GetMeshRefs() const { return m_meshRefs; }
        
        /// @brief Gets mutable mesh references.
        /// @return Mutable reference to mesh references vector.
        std::vector<WorldModelMeshRef>& GetMeshRefs() { return m_meshRefs; }
        
        /// @brief Adds a mesh reference to this group.
        /// @param meshRef The mesh reference to add.
        /// @return Index of the added mesh reference.
        size_t AddMeshRef(const WorldModelMeshRef& meshRef);
        
        /// @brief Removes a mesh reference by index.
        /// @param index The index of the mesh reference to remove.
        void RemoveMeshRef(size_t index);
        
        /// @brief Gets a mesh reference by index.
        /// @param index The index of the mesh reference.
        /// @return Pointer to the mesh reference, or nullptr if out of range.
        WorldModelMeshRef* GetMeshRef(size_t index);
        
        /// @brief Gets a mesh reference by index (const).
        /// @param index The index of the mesh reference.
        /// @return Const pointer to the mesh reference, or nullptr if out of range.
        const WorldModelMeshRef* GetMeshRef(size_t index) const;

        // Containment Volumes
        
        /// @brief Gets the containment volumes for this group.
        /// @return Const reference to containment volumes vector.
        const std::vector<ContainmentVolume>& GetContainmentVolumes() const { return m_containmentVolumes; }
        
        /// @brief Gets mutable containment volumes.
        /// @return Mutable reference to containment volumes vector.
        std::vector<ContainmentVolume>& GetContainmentVolumes() { return m_containmentVolumes; }
        
        /// @brief Adds a containment volume to this group.
        /// @param volume The containment volume to add.
        /// @return Index of the added containment volume.
        size_t AddContainmentVolume(const ContainmentVolume& volume);
        
        /// @brief Removes a containment volume by index.
        /// @param index The index of the containment volume to remove.
        void RemoveContainmentVolume(size_t index);
        
        /// @brief Gets a containment volume by index.
        /// @param index The index of the containment volume.
        /// @return Pointer to the containment volume, or nullptr if out of range.
        ContainmentVolume* GetContainmentVolume(size_t index);
        
        /// @brief Gets a containment volume by index (const).
        /// @param index The index of the containment volume.
        /// @return Const pointer to the containment volume, or nullptr if out of range.
        const ContainmentVolume* GetContainmentVolume(size_t index) const;
        
        /// @brief Checks if a point is inside this group using containment volumes.
        /// If no containment volumes are defined, falls back to AABB check.
        /// @param point The point to test (in group local space).
        /// @return True if the point is inside the group.
        bool ContainsPoint(const Vector3& point) const;

    private:
        String m_name;
        uint32 m_flags;
        AABB m_boundingBox;
        uint32 m_ambientColor;
        
        // Mesh references (new - preferred way to add geometry)
        std::vector<WorldModelMeshRef> m_meshRefs;
        
        // Containment volumes for accurate point-in-group testing
        std::vector<ContainmentVolume> m_containmentVolumes;
        
        // References
        std::vector<WorldModelPortalRef> m_portalRefs;
        std::vector<uint16> m_lightRefs;
        std::vector<uint16> m_doodadRefs;
    };

    /// @brief Represents a complete World Model Object.
    /// World Models are complex geometry objects that can contain multiple groups (rooms),
    /// connected by portals for visibility culling. They support both interior and exterior
    /// areas, lighting, doodads (props), and fog.
    class WorldModel
    {
    public:
        /// @brief Creates an empty world model.
        WorldModel();
        
        /// @brief Creates a world model with a name.
        /// @param name The name of the world model.
        explicit WorldModel(const String& name);
        
        ~WorldModel();

    public:
        /// @brief Gets the name of this world model.
        /// @return The world model name.
        const String& GetName() const { return m_name; }
        
        /// @brief Sets the name of this world model.
        /// @param name The new name.
        void SetName(const String& name) { m_name = name; }
        
        /// @brief Gets the bounding box of the entire world model.
        /// @return The bounding box.
        const AABB& GetBoundingBox() const { return m_boundingBox; }
        
        /// @brief Recalculates the bounding box from all groups.
        void RecalculateBoundingBox();
        
        /// @brief Gets the ambient color.
        /// @return The ambient color.
        uint32 GetAmbientColor() const { return m_ambientColor; }
        
        /// @brief Sets the ambient color.
        /// @param color The new ambient color.
        void SetAmbientColor(uint32 color) { m_ambientColor = color; }

        // Groups
        
        /// @brief Gets the number of groups.
        /// @return The group count.
        size_t GetGroupCount() const { return m_groups.size(); }
        
        /// @brief Gets a group by index.
        /// @param index The group index.
        /// @return Pointer to the group, or nullptr if out of range.
        WorldModelGroup* GetGroup(size_t index);
        
        /// @brief Gets a group by index (const).
        /// @param index The group index.
        /// @return Const pointer to the group, or nullptr if out of range.
        const WorldModelGroup* GetGroup(size_t index) const;
        
        /// @brief Adds a new group.
        /// @return Reference to the newly created group.
        WorldModelGroup& AddGroup();
        
        /// @brief Removes a group by index.
        /// @param index The group index to remove.
        void RemoveGroup(size_t index);

        // Portals
        
        /// @brief Gets all portals in this world model.
        /// @return Vector of portals.
        const std::vector<std::unique_ptr<Portal>>& GetPortals() const { return m_portals; }
        
        /// @brief Gets mutable portals.
        std::vector<std::unique_ptr<Portal>>& GetPortals() { return m_portals; }
        
        /// @brief Adds a new portal.
        /// @return Reference to the newly created portal.
        Portal& AddPortal();
        
        /// @brief Removes a portal by index.
        /// @param index The portal index to remove.
        void RemovePortal(size_t index);

        // Materials
        
        /// @brief Gets the list of material names.
        const std::vector<String>& GetMaterialNames() const { return m_materialNames; }
        
        /// @brief Gets mutable material names.
        std::vector<String>& GetMaterialNames() { return m_materialNames; }

        // Doodads
        
        /// @brief Gets all doodad definitions.
        const std::vector<WorldModelDoodad>& GetDoodads() const { return m_doodads; }
        
        /// @brief Gets mutable doodad definitions.
        std::vector<WorldModelDoodad>& GetDoodads() { return m_doodads; }
        
        /// @brief Gets all doodad sets.
        const std::vector<WorldModelDoodadSet>& GetDoodadSets() const { return m_doodadSets; }
        
        /// @brief Gets mutable doodad sets.
        std::vector<WorldModelDoodadSet>& GetDoodadSets() { return m_doodadSets; }
        
        /// @brief Gets the doodad name list.
        const std::vector<String>& GetDoodadNames() const { return m_doodadNames; }
        
        /// @brief Gets mutable doodad name list.
        std::vector<String>& GetDoodadNames() { return m_doodadNames; }

        // Lights
        
        /// @brief Gets all lights.
        const std::vector<WorldModelLight>& GetLights() const { return m_lights; }
        
        /// @brief Gets mutable lights.
        std::vector<WorldModelLight>& GetLights() { return m_lights; }

        // Fog
        
        /// @brief Gets all fog definitions.
        const std::vector<WorldModelFog>& GetFogs() const { return m_fogs; }
        
        /// @brief Gets mutable fog definitions.
        std::vector<WorldModelFog>& GetFogs() { return m_fogs; }

        // Group Info (for quick access without loading full groups)
        
        /// @brief Gets group info list.
        const std::vector<WorldModelGroupInfo>& GetGroupInfos() const { return m_groupInfos; }
        
        /// @brief Gets mutable group info list.
        std::vector<WorldModelGroupInfo>& GetGroupInfos() { return m_groupInfos; }

        // Child WMO References
        
        /// @brief Gets all child WMO references.
        /// @return Const reference to child WMO references vector.
        const std::vector<WorldModelChildRef>& GetChildRefs() const { return m_childRefs; }
        
        /// @brief Gets mutable child WMO references.
        /// @return Mutable reference to child WMO references vector.
        std::vector<WorldModelChildRef>& GetChildRefs() { return m_childRefs; }
        
        /// @brief Adds a child WMO reference.
        /// @param childRef The child reference to add.
        /// @return Index of the added child reference.
        size_t AddChildRef(const WorldModelChildRef& childRef);
        
        /// @brief Removes a child WMO reference by index.
        /// @param index The index of the child reference to remove.
        void RemoveChildRef(size_t index);
        
        /// @brief Gets a child WMO reference by index.
        /// @param index The index of the child reference.
        /// @return Pointer to the child reference, or nullptr if out of range.
        WorldModelChildRef* GetChildRef(size_t index);
        
        /// @brief Gets a child WMO reference by index (const).
        /// @param index The index of the child reference.
        /// @return Const pointer to the child reference, or nullptr if out of range.
        const WorldModelChildRef* GetChildRef(size_t index) const;

    private:
        String m_name;
        AABB m_boundingBox;
        uint32 m_ambientColor;
        
        std::vector<std::unique_ptr<WorldModelGroup>> m_groups;
        std::vector<std::unique_ptr<Portal>> m_portals;
        std::vector<WorldModelGroupInfo> m_groupInfos;
        
        std::vector<String> m_materialNames;
        
        std::vector<WorldModelDoodad> m_doodads;
        std::vector<WorldModelDoodadSet> m_doodadSets;
        std::vector<String> m_doodadNames;
        
        std::vector<WorldModelLight> m_lights;
        std::vector<WorldModelFog> m_fogs;
        
        // Child WMO references for nesting world models
        std::vector<WorldModelChildRef> m_childRefs;
    };

    typedef std::shared_ptr<WorldModel> WorldModelPtr;
}
