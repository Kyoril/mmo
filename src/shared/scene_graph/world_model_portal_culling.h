// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/aabb.h"
#include "math/matrix4.h"
#include "math/plane.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
    class Camera;
    class WorldModel;

    /// @brief Represents a frustum for portal culling, built from camera through portal vertices.
    /// This frustum narrows as we traverse through portals.
    struct PortalFrustum
    {
        /// @brief The planes that define this frustum. A point is inside when it is on the positive side of all.
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
        /// @return False only if every vertex lies behind one and the same plane.
        bool IsPortalVisible(const std::vector<Vector3>& vertices) const;

        /// @brief Creates a new frustum narrowed through the given portal.
        /// @param portalVertices The vertices of the portal in world space, in winding order.
        /// @return A new PortalFrustum that is the intersection of this frustum and the portal.
        PortalFrustum ClipThroughPortal(const std::vector<Vector3>& portalVertices) const;

        /// @brief Creates an initial portal frustum from a camera.
        /// The far plane is left out: draw distance is the scene's business, and a portal beyond the
        /// far plane would otherwise hide whole rooms whose nearer parts are well within it.
        /// @param camera The camera to build the frustum from.
        /// @return A PortalFrustum representing the camera's view frustum.
        static PortalFrustum FromCamera(const Camera& camera);
    };

    /// @brief How far from a portal's plane the eye still counts as standing in its doorway.
    /// In a doorway both rooms the portal joins are treated as the eye's room: the eye is about to be
    /// on either side, the room volumes may meet slightly off the portal plane, and a frustum
    /// narrowed through a portal the eye is touching is degenerate.
    constexpr float PortalDoorwayDistance = 0.5f;

    /// @brief Upper bound on portal traversals per culling pass. Rooms may be entered again through
    /// a different portal (see CollectVisiblePortalGroups); this caps the cost of pathological models.
    constexpr uint32 MaxPortalTraversals = 1024;

    /// @brief Checks whether a point stands in a portal's doorway: within PortalDoorwayDistance of the
    /// portal's plane and inside its outline (grown by the same distance).
    /// @param point The point to test.
    /// @param portalVertices The portal's vertices, in the same space as the point.
    /// @return True if the point is in the doorway.
    bool IsInPortalDoorway(const Vector3& point, const std::vector<Vector3>& portalVertices);

    /// @brief Determines which groups (rooms) of a world model are visible through its portals.
    ///
    /// Traversal starts in every group whose containment volumes hold the eye, plus both groups of any
    /// portal whose doorway the eye stands in. From there it walks the portal graph, narrowing the
    /// frustum through each visible portal. A group may be entered again through another portal, since
    /// the first path that reached it can have seen it through a narrower opening than a later one;
    /// only a group already on the current path is skipped. If the eye is in no group, every exterior
    /// group and every group whose bounds are in view is a starting point instead.
    ///
    /// @param model The world model.
    /// @param modelToWorld Transform from model space to world space (the instance's node transform).
    /// @param viewFrustum The eye's frustum in world space (see PortalFrustum::FromCamera).
    /// @param visibleGroups Receives the visible group indices, each once. Cleared first.
    void CollectVisiblePortalGroups(
        const WorldModel& model,
        const Matrix4& modelToWorld,
        const PortalFrustum& viewFrustum,
        std::vector<int32>& visibleGroups);
}
