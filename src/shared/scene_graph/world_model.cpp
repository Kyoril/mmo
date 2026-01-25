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

    // ==================== WorldModel ====================

    WorldModel::WorldModel()
        : m_ambientColor(0xFFFFFFFF)
    {
    }

    WorldModel::WorldModel(const String& name)
        : m_name(name)
        , m_ambientColor(0xFFFFFFFF)
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
}
