// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/id_generator.h"
#include "math/vector3.h"
#include "math/quaternion.h"

#include <memory>
#include <vector>

namespace mmo
{
    class Scene;
    class Entity;
    class SceneNode;
    class WorldEditor;
    class MapEntity;
    class SceneOutlineWindow;
    class Light;
    class Vector4;

    namespace proto
    {
        class UnitSpawnEntry;
        class ObjectSpawnEntry;
    }

    /// @brief Factory class for creating entities and spawns in the world editor.
    /// Handles entity creation, scene node setup, and ID generation.
    class EntityFactory final : public NonCopyable
    {
    public:
        /// @brief Constructs the entity factory.
        /// @param scene Reference to the scene where entities will be created.
        /// @param editor Reference to the world editor for project access.
        /// @param mapEntities Reference to the map entity container.
        /// @param sceneOutlineWindow Pointer to the scene outline window for updates.
        explicit EntityFactory(
            Scene &scene,
            WorldEditor &editor,
            std::vector<std::unique_ptr<MapEntity>> &mapEntities,
            SceneOutlineWindow *sceneOutlineWindow);

        ~EntityFactory() override = default;

    public:
        /// @brief Creates a map entity in the scene.
        /// @param assetName The mesh asset name to use.
        /// @param position World position for the entity.
        /// @param orientation World orientation for the entity.
        /// @param scale World scale for the entity.
        /// @param objectId Unique object ID (0 to auto-generate).
        /// @return Pointer to the created entity, or nullptr on failure.
        Entity *CreateMapEntity(const String &assetName, const Vector3 &position, const Quaternion &orientation, const Vector3 &scale, uint64 objectId);

        /// @brief Creates a point light in the scene.
        /// @param position World position for the light.
        /// @param color Light color (RGBA).
        /// @param intensity Light intensity.
        /// @param range Light range.
        /// @param objectId Unique object ID (0 to auto-generate).
        /// @return Pointer to the created light, or nullptr on failure.
        Light *CreatePointLight(const Vector3 &position, const Vector4 &color, float intensity, float range, uint64 objectId);

        /// @brief Creates a unit spawn entity in the scene.
        /// @param spawn Reference to the unit spawn entry data.
        /// @return Pointer to the created entity, or nullptr on failure.
        Entity *CreateUnitSpawnEntity(proto::UnitSpawnEntry &spawn);

        /// @brief Creates an object spawn entity in the scene.
        /// @param spawn Reference to the object spawn entry data.
        /// @return Pointer to the created entity, or nullptr on failure.
        Entity *CreateObjectSpawnEntity(proto::ObjectSpawnEntry &spawn);

        /// @brief Generates a unique ID for a new map entity.
        /// @return A new unique ID.
        uint64 GenerateUniqueId();

        /// @brief Notifies the factory of an existing ID to avoid collisions.
        /// @param id The existing ID to register.
        void NotifyExistingId(uint64 id);

        /// @brief Generates a unique ID for a unit spawn.
        /// @return A new unique unit spawn ID.
        uint32 GenerateUnitSpawnId();

        /// @brief Generates a unique ID for an object spawn.
        /// @return A new unique object spawn ID.
        uint32 GenerateObjectSpawnId();

        /// @brief Resets the unit spawn ID generator.
        void ResetUnitSpawnIdGenerator();

        /// @brief Resets the object spawn ID generator.
        void ResetObjectSpawnIdGenerator();

    private:
        Scene &m_scene;
        WorldEditor &m_editor;
        std::vector<std::unique_ptr<MapEntity>> &m_mapEntities;
        SceneOutlineWindow *m_sceneOutlineWindow;

        IdGenerator<uint64> m_objectIdGenerator{1};
        IdGenerator<uint64> m_unitSpawnIdGenerator{1};
        IdGenerator<uint64> m_objectSpawnIdGenerator{1};
    };
}
