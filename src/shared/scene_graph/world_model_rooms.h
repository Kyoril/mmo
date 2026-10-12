// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/aabb.h"

#include <functional>
#include <vector>

namespace mmo
{
    class WorldModel;
    class WorldModelGroup;
    struct ContainmentVolume;

    /// @brief Looks up a mesh's model-space bounds by its path, or returns nullptr if the mesh cannot
    /// be loaded. The editor answers from its mesh manager; tests answer from a table.
    using WorldModelMeshBoundsLookup = std::function<const AABB*(const String& meshPath)>;

    /// @brief Grid the floor footprints are rasterised on when deriving room volumes, in units.
    constexpr float RoomVolumeCellSize = 0.5f;

    /// @brief How far below its floor a derived room volume starts, so a camera dipping under a
    /// floor edge (stairs, uneven tiles) still counts as inside the room.
    constexpr float RoomVolumeBelowFloor = 1.5f;

    /// @brief Checks whether a mesh is a piece a player can stand on (floor, platform, stair, ramp,
    /// bridge), judged by its file name. These pieces' footprints make up a room.
    /// @param meshPath The mesh's asset path.
    /// @return True if the mesh is walkable.
    bool IsWalkableRoomPiece(const String& meshPath);

    /// @brief Derives a group's containment volumes from its walkable pieces.
    ///
    /// Portal culling starts in the room that contains the camera. Without containment volumes a
    /// group falls back to its bounding box, and the box of a modular room includes every wall,
    /// window and arch assigned to it, so neighbouring rooms' boxes overlap and the camera gets
    /// credited to rooms it is not in. Floors, by contrast, end where the room ends.
    ///
    /// The walkable pieces' footprints are rasterised on a RoomVolumeCellSize grid and merged into as
    /// few boxes as possible, each reaching from RoomVolumeBelowFloor below its lowest floor to the
    /// group's ceiling.
    ///
    /// @param group The group (room).
    /// @param meshBounds Mesh bounds lookup.
    /// @return The volumes, empty if the group has no walkable pieces.
    std::vector<ContainmentVolume> DeriveRoomVolumes(const WorldModelGroup& group, const WorldModelMeshBoundsLookup& meshBounds);

    /// @brief Replaces the containment volumes of every group that has walkable pieces with derived ones.
    /// @param model The world model.
    /// @param meshBounds Mesh bounds lookup.
    /// @return Number of groups whose volumes were replaced. Groups without walkable pieces keep theirs.
    size_t DeriveAllRoomVolumes(WorldModel& model, const WorldModelMeshBoundsLookup& meshBounds);

    /// @brief What CheckPortalLinks found for one portal.
    struct PortalLinkFinding
    {
        enum class Result
        {
            /// @brief The portal links the rooms on its two sides.
            Ok,

            /// @brief The portal linked other rooms than the ones on its sides (and links those now,
            /// if CheckPortalLinks was asked to fix).
            Relinked,

            /// @brief The rooms on the portal's sides are unclear (none, several, or the same room on
            /// both sides); its links were kept.
            Undecided,
        };

        uint32 portalIndex { 0 };
        Result result { Result::Ok };

        /// @brief Groups the portal linked before the check, sorted.
        std::vector<int32> linkedGroups;

        /// @brief Groups found in front of and behind the portal.
        std::vector<int32> frontGroups;
        std::vector<int32> backGroups;
    };

    /// @brief Checks every portal's links against the rooms on its two sides, sampled a little in front
    /// of and behind its centre with the groups' containment test, and relinks mislinked portals.
    /// Portal references are sorted by portal index afterwards.
    /// @param model The world model. Its groups should have containment volumes (see DeriveAllRoomVolumes).
    /// @param fix Whether to relink mislinked portals, or only report them.
    /// @return One finding per portal.
    std::vector<PortalLinkFinding> CheckPortalLinks(WorldModel& model, bool fix);
}
