// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_rooms.h"
#include "world_model.h"

#include "math/matrix4.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace mmo
{
    namespace
    {
        /// Distances in front of and behind a portal at which the rooms on its sides are looked for.
        constexpr float PortalProbeDistances[] = { 0.75f, 1.5f, 2.5f };

        /// Heights relative to the portal centre to probe at: a tall portal's centre can sit above a
        /// low room's ceiling.
        constexpr float PortalProbeLifts[] = { 0.0f, -0.25f };

        String FileStemLower(const String& path)
        {
            const size_t slash = path.find_last_of("/\\");
            String stem = slash == String::npos ? path : path.substr(slash + 1);
            const size_t dot = stem.find_last_of('.');
            if (dot != String::npos)
            {
                stem.resize(dot);
            }
            std::transform(stem.begin(), stem.end(), stem.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return stem;
        }

        std::vector<int32> GroupsContaining(const WorldModel& model, const Vector3& point)
        {
            std::vector<int32> groups;
            for (size_t i = 0; i < model.GetGroupCount(); ++i)
            {
                const WorldModelGroup* group = model.GetGroup(i);
                if (group && group->ContainsPoint(point))
                {
                    groups.push_back(static_cast<int32>(i));
                }
            }
            return groups;
        }

        /// The groups on one side of a portal: those at the nearest probe distance that finds any.
        std::vector<int32> GroupsOnSide(const WorldModel& model, const Vector3& center, const Vector3& normal, const float sign)
        {
            for (const float distance : PortalProbeDistances)
            {
                std::vector<int32> found;
                for (const float lift : PortalProbeLifts)
                {
                    const Vector3 probe = center + normal * (distance * sign) + Vector3(0.0f, lift, 0.0f);
                    for (const int32 group : GroupsContaining(model, probe))
                    {
                        if (std::find(found.begin(), found.end(), group) == found.end())
                        {
                            found.push_back(group);
                        }
                    }
                }

                if (!found.empty())
                {
                    std::sort(found.begin(), found.end());
                    return found;
                }
            }

            return {};
        }

        /// Greedy rectangle cover of a row-major boolean grid: [row0, row1) x [col0, col1) rectangles.
        struct CellRect
        {
            size_t row0, col0, row1, col1;
        };

        std::vector<CellRect> MergeCells(std::vector<bool> occupied, const size_t rows, const size_t cols)
        {
            std::vector<CellRect> rects;
            for (size_t i = 0; i < rows; ++i)
            {
                size_t j = 0;
                while (j < cols)
                {
                    if (!occupied[i * cols + j])
                    {
                        ++j;
                        continue;
                    }

                    size_t j1 = j;
                    while (j1 < cols && occupied[i * cols + j1])
                    {
                        ++j1;
                    }

                    size_t i1 = i + 1;
                    while (i1 < rows)
                    {
                        bool full = true;
                        for (size_t k = j; k < j1 && full; ++k)
                        {
                            full = occupied[i1 * cols + k];
                        }
                        if (!full)
                        {
                            break;
                        }
                        ++i1;
                    }

                    for (size_t r = i; r < i1; ++r)
                    {
                        for (size_t k = j; k < j1; ++k)
                        {
                            occupied[r * cols + k] = false;
                        }
                    }

                    rects.push_back({ i, j, i1, j1 });
                    j = j1;
                }
            }
            return rects;
        }
    }

    bool IsWalkableRoomPiece(const String& meshPath)
    {
        static const char* const walkable[] = { "floor", "platform", "stair", "ramp", "bridge" };

        const String stem = FileStemLower(meshPath);
        for (const char* const key : walkable)
        {
            if (stem.find(key) != String::npos)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<ContainmentVolume> DeriveRoomVolumes(const WorldModelGroup& group, const WorldModelMeshBoundsLookup& meshBounds)
    {
        std::vector<AABB> pieces;
        for (const WorldModelMeshRef& meshRef : group.GetMeshRefs())
        {
            if (!meshRef.visible || !IsWalkableRoomPiece(meshRef.meshPath))
            {
                continue;
            }

            const AABB* bounds = meshBounds ? meshBounds(meshRef.meshPath) : nullptr;
            if (!bounds || bounds->IsNull())
            {
                continue;
            }

            Matrix4 transform;
            transform.MakeTransform(meshRef.position, meshRef.scale, meshRef.rotation);
            AABB piece = *bounds;
            piece.Transform(transform);
            pieces.push_back(piece);
        }

        if (pieces.empty())
        {
            return {};
        }

        AABB footprint = pieces.front();
        for (const AABB& piece : pieces)
        {
            footprint.Combine(piece);
        }

        const float cell = RoomVolumeCellSize;
        const float x0 = std::floor(footprint.min.x / cell) * cell;
        const float z0 = std::floor(footprint.min.z / cell) * cell;
        const size_t cols = static_cast<size_t>(std::ceil((footprint.max.x - x0) / cell));
        const size_t rows = static_cast<size_t>(std::ceil((footprint.max.z - z0) / cell));
        if (cols == 0 || rows == 0)
        {
            return {};
        }

        // Lowest floor height per cell; infinity where there is no floor
        constexpr float noFloor = std::numeric_limits<float>::infinity();
        std::vector<float> floorHeight(rows * cols, noFloor);
        for (const AABB& piece : pieces)
        {
            const auto toCell = [cell](const float value, const float origin, const size_t limit)
            {
                const long index = std::lround((value - origin) / cell);
                return static_cast<size_t>(std::clamp<long>(index, 0, static_cast<long>(limit)));
            };

            const size_t j0 = toCell(piece.min.x, x0, cols);
            const size_t i0 = toCell(piece.min.z, z0, rows);
            const size_t j1 = std::min(std::max(toCell(piece.max.x, x0, cols), j0 + 1), cols);
            const size_t i1 = std::min(std::max(toCell(piece.max.z, z0, rows), i0 + 1), rows);
            for (size_t i = i0; i < i1; ++i)
            {
                for (size_t j = j0; j < j1; ++j)
                {
                    float& height = floorHeight[i * cols + j];
                    height = std::min(height, piece.min.y);
                }
            }
        }

        std::vector<bool> occupied(rows * cols);
        for (size_t k = 0; k < occupied.size(); ++k)
        {
            occupied[k] = floorHeight[k] != noFloor;
        }

        const float ceiling = std::max(group.GetBoundingBox().IsNull() ? footprint.max.y : group.GetBoundingBox().max.y, footprint.max.y) + 0.5f;
        const String baseName = (group.GetName().empty() ? String("Group") : group.GetName()) + " floor ";

        std::vector<ContainmentVolume> volumes;
        for (const CellRect& rect : MergeCells(std::move(occupied), rows, cols))
        {
            float bottom = noFloor;
            for (size_t i = rect.row0; i < rect.row1; ++i)
            {
                for (size_t j = rect.col0; j < rect.col1; ++j)
                {
                    bottom = std::min(bottom, floorHeight[i * cols + j]);
                }
            }

            const AABB box(
                Vector3(x0 + static_cast<float>(rect.col0) * cell, bottom - RoomVolumeBelowFloor, z0 + static_cast<float>(rect.row0) * cell),
                Vector3(x0 + static_cast<float>(rect.col1) * cell, ceiling, z0 + static_cast<float>(rect.row1) * cell));
            volumes.push_back(ContainmentVolume::FromAABB(box, baseName + std::to_string(volumes.size() + 1)));
        }

        return volumes;
    }

    size_t DeriveAllRoomVolumes(WorldModel& model, const WorldModelMeshBoundsLookup& meshBounds)
    {
        size_t replaced = 0;
        for (size_t i = 0; i < model.GetGroupCount(); ++i)
        {
            WorldModelGroup* group = model.GetGroup(i);
            if (!group)
            {
                continue;
            }

            std::vector<ContainmentVolume> volumes = DeriveRoomVolumes(*group, meshBounds);
            if (volumes.empty())
            {
                continue;
            }

            group->GetContainmentVolumes() = std::move(volumes);
            ++replaced;
        }
        return replaced;
    }

    std::vector<PortalLinkFinding> CheckPortalLinks(WorldModel& model, const bool fix)
    {
        std::vector<PortalLinkFinding> findings;

        const auto& portals = model.GetPortals();
        for (size_t portalIndex = 0; portalIndex < portals.size(); ++portalIndex)
        {
            PortalLinkFinding finding;
            finding.portalIndex = static_cast<uint32>(portalIndex);

            for (size_t g = 0; g < model.GetGroupCount(); ++g)
            {
                for (const auto& ref : model.GetGroup(g)->GetPortalRefs())
                {
                    if (ref.portalIndex == portalIndex)
                    {
                        finding.linkedGroups.push_back(static_cast<int32>(g));
                        break;
                    }
                }
            }

            const std::vector<Vector3>& vertices = portals[portalIndex] ? portals[portalIndex]->GetWorldVertices() : std::vector<Vector3>{};
            Vector3 normal = vertices.size() >= 3 ? (vertices[1] - vertices[0]).Cross(vertices[2] - vertices[0]) : Vector3::Zero;
            if (normal.GetLength() < 0.0001f)
            {
                finding.result = PortalLinkFinding::Result::Undecided;
                findings.push_back(std::move(finding));
                continue;
            }
            normal.Normalize();

            Vector3 center = Vector3::Zero;
            for (const Vector3& vertex : vertices)
            {
                center += vertex;
            }
            center /= static_cast<float>(vertices.size());

            finding.frontGroups = GroupsOnSide(model, center, normal, 1.0f);
            finding.backGroups = GroupsOnSide(model, center, normal, -1.0f);

            if (finding.frontGroups.size() != 1 || finding.backGroups.size() != 1 || finding.frontGroups == finding.backGroups)
            {
                finding.result = PortalLinkFinding::Result::Undecided;
                findings.push_back(std::move(finding));
                continue;
            }

            const int32 groupA = std::min(finding.frontGroups.front(), finding.backGroups.front());
            const int32 groupB = std::max(finding.frontGroups.front(), finding.backGroups.front());
            if (finding.linkedGroups == std::vector<int32>{ groupA, groupB })
            {
                findings.push_back(std::move(finding));
                continue;
            }

            finding.result = PortalLinkFinding::Result::Relinked;
            if (fix)
            {
                for (size_t g = 0; g < model.GetGroupCount(); ++g)
                {
                    auto& refs = model.GetGroup(g)->GetPortalRefs();
                    refs.erase(std::remove_if(refs.begin(), refs.end(),
                        [portalIndex](const WorldModelPortalRef& ref) { return ref.portalIndex == portalIndex; }), refs.end());
                }

                const uint16 index = static_cast<uint16>(portalIndex);
                model.GetGroup(groupA)->GetPortalRefs().push_back(WorldModelPortalRef{ index, static_cast<uint16>(groupB), 1 });
                model.GetGroup(groupB)->GetPortalRefs().push_back(WorldModelPortalRef{ index, static_cast<uint16>(groupA), -1 });
            }
            findings.push_back(std::move(finding));
        }

        if (fix)
        {
            for (size_t g = 0; g < model.GetGroupCount(); ++g)
            {
                auto& refs = model.GetGroup(g)->GetPortalRefs();
                std::stable_sort(refs.begin(), refs.end(),
                    [](const WorldModelPortalRef& a, const WorldModelPortalRef& b) { return a.portalIndex < b.portalIndex; });
            }
        }

        return findings;
    }
}
