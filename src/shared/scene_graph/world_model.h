// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"
#include "math/vector3.h"
#include "math/quaternion.h"
#include "math/aabb.h"
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

        /// @brief Gets the vertex positions.
        const std::vector<Vector3>& GetVertices() const { return m_vertices; }
        
        /// @brief Gets mutable vertex positions.
        std::vector<Vector3>& GetVertices() { return m_vertices; }
        
        /// @brief Gets the vertex normals.
        const std::vector<Vector3>& GetNormals() const { return m_normals; }
        
        /// @brief Gets mutable vertex normals.
        std::vector<Vector3>& GetNormals() { return m_normals; }
        
        /// @brief Gets the vertex texture coordinates.
        const std::vector<Vector3>& GetTexCoords() const { return m_texCoords; }
        
        /// @brief Gets mutable vertex texture coordinates.
        std::vector<Vector3>& GetTexCoords() { return m_texCoords; }
        
        /// @brief Gets the vertex colors.
        const std::vector<uint32>& GetVertexColors() const { return m_vertexColors; }
        
        /// @brief Gets mutable vertex colors.
        std::vector<uint32>& GetVertexColors() { return m_vertexColors; }
        
        /// @brief Gets the triangle indices.
        const std::vector<uint32>& GetIndices() const { return m_indices; }
        
        /// @brief Gets mutable triangle indices.
        std::vector<uint32>& GetIndices() { return m_indices; }
        
        /// @brief Gets the material indices for each triangle.
        const std::vector<uint16>& GetMaterialIndices() const { return m_materialIndices; }
        
        /// @brief Gets mutable material indices.
        std::vector<uint16>& GetMaterialIndices() { return m_materialIndices; }

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

    private:
        String m_name;
        uint32 m_flags;
        AABB m_boundingBox;
        uint32 m_ambientColor;
        
        // Geometry data
        std::vector<Vector3> m_vertices;
        std::vector<Vector3> m_normals;
        std::vector<Vector3> m_texCoords;
        std::vector<uint32> m_vertexColors;
        std::vector<uint32> m_indices;
        std::vector<uint16> m_materialIndices;
        
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
    };

    typedef std::shared_ptr<WorldModel> WorldModelPtr;
}
