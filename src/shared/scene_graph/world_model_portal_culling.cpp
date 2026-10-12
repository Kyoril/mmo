// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_portal_culling.h"
#include "world_model.h"
#include "camera.h"

#include <cmath>

namespace mmo
{
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
            if (plane.GetSide(center, halfSize) == Plane::NegativeSide)
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
                return false;
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

        // Build planes from the eye through each edge of the portal
        const size_t numVerts = portalVertices.size();
        for (size_t i = 0; i < numVerts; ++i)
        {
            const Vector3& v0 = portalVertices[i];
            const Vector3& v1 = portalVertices[(i + 1) % numVerts];

            Vector3 planeNormal = (v1 - v0).Cross(v0 - origin);
            const float length = planeNormal.GetLength();
            if (length < 0.0001f)
            {
                continue; // Degenerate edge, skip
            }
            planeNormal /= length;

            // The portal center must be on the positive side (inside the visible cone)
            if (planeNormal.Dot(portalCenter - origin) < 0)
            {
                planeNormal = -planeNormal;
            }

            newFrustum.planes.emplace_back(planeNormal, origin);
        }

        // The portal's own plane: only what lies beyond the portal, seen from the eye, is visible
        Vector3 portalNormal = (portalVertices[1] - portalVertices[0]).Cross(portalVertices[2] - portalVertices[0]);
        const float length = portalNormal.GetLength();
        if (length > 0.0001f)
        {
            portalNormal /= length;
            if (portalNormal.Dot(origin - portalVertices[0]) > 0)
            {
                portalNormal = -portalNormal;
            }
            newFrustum.planes.emplace_back(portalNormal, portalVertices[0]);
        }

        // Intersect with the frustum we came through
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

        Plane cameraPlanes[6];
        camera.ExtractFrustumPlanes(cameraPlanes);
        for (int i = 0; i < 6; ++i)
        {
            if (i != FrustumPlaneFar)
            {
                frustum.planes.push_back(cameraPlanes[i]);
            }
        }

        return frustum;
    }

    bool IsInPortalDoorway(const Vector3& point, const std::vector<Vector3>& portalVertices)
    {
        if (portalVertices.size() < 3)
        {
            return false;
        }

        Vector3 center(0, 0, 0);
        for (const Vector3& v : portalVertices)
        {
            center += v;
        }
        center /= static_cast<float>(portalVertices.size());

        Vector3 normal = (portalVertices[1] - portalVertices[0]).Cross(portalVertices[2] - portalVertices[0]);
        const float length = normal.GetLength();
        if (length < 0.0001f)
        {
            return false;
        }
        normal /= length;

        const float distance = normal.Dot(point - center);
        if (std::abs(distance) > PortalDoorwayDistance)
        {
            return false;
        }

        // Inside the outline, grown by the doorway distance: on the inner side of every edge
        const Vector3 onPlane = point - normal * distance;
        const size_t count = portalVertices.size();
        for (size_t i = 0; i < count; ++i)
        {
            const Vector3& v0 = portalVertices[i];
            const Vector3& v1 = portalVertices[(i + 1) % count];

            Vector3 inward = normal.Cross(v1 - v0);
            const float edgeLength = inward.GetLength();
            if (edgeLength < 0.0001f)
            {
                continue;
            }
            inward /= edgeLength;
            if (inward.Dot(center - v0) < 0)
            {
                inward = -inward;
            }

            if (inward.Dot(onPlane - v0) < -PortalDoorwayDistance)
            {
                return false;
            }
        }

        return true;
    }

    namespace
    {
        class PortalGroupCollector final
        {
        public:
            PortalGroupCollector(const WorldModel& model, const Matrix4& modelToWorld, std::vector<int32>& visibleGroups)
                : m_model(model)
                , m_visibleGroups(visibleGroups)
                , m_visible(model.GetGroupCount(), false)
                , m_onPath(model.GetGroupCount(), false)
            {
                // Every portal in world space, once per pass
                const auto& portals = model.GetPortals();
                m_portalVertices.resize(portals.size());
                for (size_t i = 0; i < portals.size(); ++i)
                {
                    if (portals[i] && portals[i]->IsActive())
                    {
                        m_portalVertices[i] = portals[i]->GetWorldVertices();
                        for (Vector3& vertex : m_portalVertices[i])
                        {
                            vertex = modelToWorld * vertex;
                        }
                    }
                }
            }

        public:
            void MarkVisible(const int32 groupIndex)
            {
                if (!m_visible[groupIndex])
                {
                    m_visible[groupIndex] = true;
                    m_visibleGroups.push_back(groupIndex);
                }
            }

            bool IsVisible(const int32 groupIndex) const
            {
                return m_visible[groupIndex];
            }

            const std::vector<Vector3>& GetPortalVertices(const size_t portalIndex) const
            {
                return m_portalVertices[portalIndex];
            }

            void Traverse(const int32 groupIndex, const PortalFrustum& frustum)
            {
                if (groupIndex < 0 || groupIndex >= static_cast<int32>(m_onPath.size()) || m_onPath[groupIndex])
                {
                    return;
                }

                const WorldModelGroup* group = m_model.GetGroup(static_cast<size_t>(groupIndex));
                if (!group)
                {
                    return;
                }

                MarkVisible(groupIndex);
                m_onPath[groupIndex] = true;

                for (const auto& portalRef : group->GetPortalRefs())
                {
                    const int32 target = portalRef.groupIndex;
                    if (target >= static_cast<int32>(m_onPath.size()) || m_onPath[target] ||
                        portalRef.portalIndex >= m_portalVertices.size())
                    {
                        continue;
                    }

                    const std::vector<Vector3>& vertices = m_portalVertices[portalRef.portalIndex];
                    if (vertices.empty() || m_traversals >= MaxPortalTraversals)
                    {
                        continue;
                    }

                    if (IsInPortalDoorway(frustum.origin, vertices))
                    {
                        // Standing in the opening: narrowing through it would be degenerate
                        ++m_traversals;
                        Traverse(target, frustum);
                    }
                    else if (frustum.IsPortalVisible(vertices))
                    {
                        ++m_traversals;
                        Traverse(target, frustum.ClipThroughPortal(vertices));
                    }
                }

                m_onPath[groupIndex] = false;
            }

        private:
            const WorldModel& m_model;
            std::vector<int32>& m_visibleGroups;
            std::vector<bool> m_visible;
            std::vector<bool> m_onPath;
            std::vector<std::vector<Vector3>> m_portalVertices;
            uint32 m_traversals = 0;
        };
    }

    void CollectVisiblePortalGroups(
        const WorldModel& model,
        const Matrix4& modelToWorld,
        const PortalFrustum& viewFrustum,
        std::vector<int32>& visibleGroups)
    {
        visibleGroups.clear();

        const size_t groupCount = model.GetGroupCount();
        if (groupCount == 0)
        {
            return;
        }

        PortalGroupCollector collector(model, modelToWorld, visibleGroups);

        // The eye's rooms: every group containing it, and both sides of any doorway it stands in.
        // Group volumes are authored per room and may overlap where rooms meet, so picking a single
        // "best" group would credit the eye to the wrong room there - and that room's portals would
        // then hide the one the eye is really in.
        const Vector3 localEye = modelToWorld.Inverse() * viewFrustum.origin;
        std::vector<int32> seeds;
        for (size_t i = 0; i < groupCount; ++i)
        {
            const WorldModelGroup* group = model.GetGroup(i);
            if (group && group->ContainsPoint(localEye))
            {
                seeds.push_back(static_cast<int32>(i));
            }
        }

        if (!seeds.empty())
        {
            for (size_t i = 0; i < groupCount; ++i)
            {
                const WorldModelGroup* group = model.GetGroup(i);
                if (!group)
                {
                    continue;
                }

                for (const auto& portalRef : group->GetPortalRefs())
                {
                    if (portalRef.portalIndex < model.GetPortals().size() &&
                        IsInPortalDoorway(viewFrustum.origin, collector.GetPortalVertices(portalRef.portalIndex)))
                    {
                        seeds.push_back(static_cast<int32>(i));
                        break;
                    }
                }
            }
        }
        else
        {
            // Outside every room: anything exterior, and any room whose bounds are in view
            for (size_t i = 0; i < groupCount; ++i)
            {
                const WorldModelGroup* group = model.GetGroup(i);
                if (!group)
                {
                    continue;
                }

                AABB worldBounds = group->GetBoundingBox();
                worldBounds.Transform(modelToWorld);
                if (group->IsExterior() || viewFrustum.IsVisible(worldBounds))
                {
                    seeds.push_back(static_cast<int32>(i));
                }
            }
        }

        for (const int32 seed : seeds)
        {
            collector.Traverse(seed, viewFrustum);
        }
    }
}
