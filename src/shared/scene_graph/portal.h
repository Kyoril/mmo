
#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/quaternion.h"
#include "scene_graph/camera.h"

#include <vector>

namespace mmo
{
    class Camera;

    enum class PortalType
    {
        OneWay,
        TwoWay,
    };

    /// This class represents a portal in the scene graph which is used for fast and easy culling of objects 
    /// between certain spaces in the game world (think of indoor/outdoor transitions).
    class Portal
    {
    public:
        Portal(uint32 id);
        ~Portal();

    public:
        void SetTransform(const Vector3& position, const Quaternion& rotation, const Vector3& scale);
        void SetDimensions(float width, float height);
        void SetPortalType(PortalType type);
        
        float GetWidth() const { return m_width; }
        float GetHeight() const { return m_height; }
        PortalType GetPortalType() const { return m_portalType; }
        uint32 GetPortalId() const { return m_portalId; }
        const Vector3& GetPosition() const { return m_position; }
        const Quaternion& GetRotation() const { return m_rotation; }
        const Vector3& GetScale() const { return m_scale; }

        // Visibility testing
        //bool IsVisibleFromCamera(const Camera& camera, const Frustum& frustum) const;
        bool IsPointBehindPortal(const Vector3& point) const;
        
        // Frustum operations
        //Frustum ClipFrustumThroughPortal(const Camera& camera, const Frustum& inputFrustum) const;
        AABB GetWorldBounds() const;
        
        // World vertices access
        const std::vector<Vector3>& GetWorldVertices();
        
        // State
        void SetActive(bool active) 
        { 
            m_isActive = active; 
        }
        
        bool IsActive() const 
        { 
            return m_isActive; 
        }
        
        // Frame tracking for optimization
        void MarkVisibleThisFrame(uint32_t frameNumber) const;
        bool WasVisibleLastFrame(uint32_t frameNumber) const;

    private:
        void UpdateWorldVertices();

    private:
        uint32 m_portalId;
        Vector3 m_position;
        Quaternion m_rotation;
        Vector3 m_scale;
        PortalType m_portalType;
        float m_width;
        float m_height;
        bool m_isActive;

        std::vector<Vector3> m_localVertices;
        std::vector<Vector3> m_worldVertices;
        bool m_verticesDirty;

        mutable bool m_lastFrameVisible;
        mutable uint32 m_lastVisibilityFrame;
    };
}