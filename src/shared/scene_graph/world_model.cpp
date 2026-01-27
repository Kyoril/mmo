// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model.h"

namespace mmo
{
    // ==================== WorldModelGroup ====================

    WorldModelGroup::WorldModelGroup()
        : m_flags(0)
        , m_ambientColor(0xFFFFFFFF)
    {
    }

    WorldModelGroup::~WorldModelGroup() = default;

    size_t WorldModelGroup::AddMeshRef(const WorldModelMeshRef& meshRef)
    {
        m_meshRefs.push_back(meshRef);
        return m_meshRefs.size() - 1;
    }

    void WorldModelGroup::RemoveMeshRef(size_t index)
    {
        if (index < m_meshRefs.size())
        {
            m_meshRefs.erase(m_meshRefs.begin() + index);
        }
    }

    WorldModelMeshRef* WorldModelGroup::GetMeshRef(size_t index)
    {
        if (index >= m_meshRefs.size())
        {
            return nullptr;
        }
        
        return &m_meshRefs[index];
    }

    const WorldModelMeshRef* WorldModelGroup::GetMeshRef(size_t index) const
    {
        if (index >= m_meshRefs.size())
        {
            return nullptr;
        }
        
        return &m_meshRefs[index];
    }

    size_t WorldModelGroup::AddContainmentVolume(const ContainmentVolume& volume)
    {
        m_containmentVolumes.push_back(volume);
        return m_containmentVolumes.size() - 1;
    }

    void WorldModelGroup::RemoveContainmentVolume(size_t index)
    {
        if (index < m_containmentVolumes.size())
        {
            m_containmentVolumes.erase(m_containmentVolumes.begin() + index);
        }
    }

    ContainmentVolume* WorldModelGroup::GetContainmentVolume(size_t index)
    {
        if (index >= m_containmentVolumes.size())
        {
            return nullptr;
        }
        
        return &m_containmentVolumes[index];
    }

    const ContainmentVolume* WorldModelGroup::GetContainmentVolume(size_t index) const
    {
        if (index >= m_containmentVolumes.size())
        {
            return nullptr;
        }
        
        return &m_containmentVolumes[index];
    }

    bool WorldModelGroup::ContainsPoint(const Vector3& point) const
    {
        // If containment volumes are defined, use them for accurate testing
        if (!m_containmentVolumes.empty())
        {
            for (const auto& volume : m_containmentVolumes)
            {
                if (volume.ContainsPoint(point))
                {
                    return true;
                }
            }
            return false;
        }
        
        // Fall back to AABB check if no containment volumes defined
        return m_boundingBox.Intersects(point);
    }

    // ==================== WorldModel ====================

    WorldModel::WorldModel()
        : m_ambientColor(0xFF333333)
    {
    }

    WorldModel::WorldModel(const String& name)
        : m_name(name)
        , m_ambientColor(0xFF333333)
    {
    }

    WorldModel::~WorldModel() = default;

    void WorldModel::RecalculateBoundingBox()
    {
        m_boundingBox.SetNull();
        
        for (const auto& group : m_groups)
        {
            if (group)
            {
                m_boundingBox.Combine(group->GetBoundingBox());
            }
        }
    }

    WorldModelGroup* WorldModel::GetGroup(size_t index)
    {
        if (index >= m_groups.size())
        {
            return nullptr;
        }
        
        return m_groups[index].get();
    }

    const WorldModelGroup* WorldModel::GetGroup(size_t index) const
    {
        if (index >= m_groups.size())
        {
            return nullptr;
        }
        
        return m_groups[index].get();
    }

    WorldModelGroup& WorldModel::AddGroup()
    {
        auto& group = m_groups.emplace_back(std::make_unique<WorldModelGroup>());
        return *group;
    }

    void WorldModel::RemoveGroup(size_t index)
    {
        if (index < m_groups.size())
        {
            m_groups.erase(m_groups.begin() + index);
        }
    }

    Portal& WorldModel::AddPortal()
    {
        auto& portal = m_portals.emplace_back(std::make_unique<Portal>(static_cast<uint32>(m_portals.size())));
        return *portal;
    }

    void WorldModel::RemovePortal(size_t index)
    {
        if (index < m_portals.size())
        {
            m_portals.erase(m_portals.begin() + index);
            
            // Update portal IDs
            for (size_t i = index; i < m_portals.size(); ++i)
            {
                // Note: Portal ID cannot be changed after creation in current implementation
                // This may need to be updated if portal removal is needed
            }
        }
    }

    size_t WorldModel::AddChildRef(const WorldModelChildRef& childRef)
    {
        m_childRefs.push_back(childRef);
        return m_childRefs.size() - 1;
    }

    void WorldModel::RemoveChildRef(size_t index)
    {
        if (index < m_childRefs.size())
        {
            m_childRefs.erase(m_childRefs.begin() + index);
        }
    }

    WorldModelChildRef* WorldModel::GetChildRef(size_t index)
    {
        if (index >= m_childRefs.size())
        {
            return nullptr;
        }
        
        return &m_childRefs[index];
    }

    const WorldModelChildRef* WorldModel::GetChildRef(size_t index) const
    {
        if (index >= m_childRefs.size())
        {
            return nullptr;
        }
        
        return &m_childRefs[index];
    }
}
