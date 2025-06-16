
#include "portal.h"

namespace mmo
{
    Portal::Portal(uint32 id)
        : m_portalId(id)
        , m_portalType(PortalType::TwoWay)
        , m_width(2.0f)
        , m_height(3.0f)
        , m_isActive(true)
        , m_verticesDirty(true)
        , m_lastFrameVisible(false)
        , m_lastVisibilityFrame(0)
    {
        // Initialize local vertices for a portal quad
        m_localVertices.resize(4);
        m_worldVertices.resize(4);
        
        // Create a portal facing forward (negative Z)
        float halfWidth = m_width * 0.5f;
        float halfHeight = m_height * 0.5f;
        
        m_localVertices[0] = Vector3(-halfWidth, -halfHeight, 0.0f); // Bottom-left
        m_localVertices[1] = Vector3(halfWidth, -halfHeight, 0.0f);  // Bottom-right
        m_localVertices[2] = Vector3(halfWidth, halfHeight, 0.0f);   // Top-right
        m_localVertices[3] = Vector3(-halfWidth, halfHeight, 0.0f);  // Top-left
    }
        
    Portal::~Portal()
    {
    }

    void Portal::SetTransform(const Vector3& position, const Quaternion& rotation, const Vector3& scale)
    {
        m_position = position;
        m_rotation = rotation;
        m_scale = scale;
        m_verticesDirty = true;
    }
        
    void Portal::SetDimensions(float width, float height)
    {
        if (m_width != width || m_height != height)
        {
            m_width = width;
            m_height = height;
            
            // Update local vertices
            float halfWidth = m_width * 0.5f;
            float halfHeight = m_height * 0.5f;
            
            m_localVertices[0] = Vector3(-halfWidth, -halfHeight, 0.0f);
            m_localVertices[1] = Vector3(halfWidth, -halfHeight, 0.0f);
            m_localVertices[2] = Vector3(halfWidth, halfHeight, 0.0f);
            m_localVertices[3] = Vector3(-halfWidth, halfHeight, 0.0f);
            
            m_verticesDirty = true;
        }
    }

    void Portal::UpdateWorldVertices()
    {
        if (!m_verticesDirty)
        {
            return;
        }
        
        Matrix4 worldMatrix;
        worldMatrix.MakeTransform(m_position, m_scale, m_rotation);
        
        for (size_t i = 0; i < m_localVertices.size(); ++i)
        {
            m_worldVertices[i] = worldMatrix * m_localVertices[i];
        }
        
        m_verticesDirty = false;
    }

    const std::vector<Vector3>& Portal::GetWorldVertices()
    {
        UpdateWorldVertices();
        return m_worldVertices;
    }

    AABB Portal::GetWorldBounds() const
    {
        // This is a simplified version - you might want to cache this
        const_cast<Portal*>(this)->UpdateWorldVertices();
        
        AABB bounds;
        bounds.SetNull();

        for (const Vector3& vertex : m_worldVertices)
        {
            bounds.Combine(vertex);
        }
        
        return bounds;
    }
}