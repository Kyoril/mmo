#pragma once

#include "math/vector3.h"
#include "base/typedefs.h"

namespace mmo
{
    /**
     * @brief Interface for drawing debug geometry primitives.
     * 
     * This interface provides methods for drawing debug geometry such as lines and triangles
     * in absolute scene coordinates. All geometry is rendered to a ManualRenderObject internally.
     */
    class IDebugGeometry
    {
    public:
        virtual ~IDebugGeometry() = default;

        /**
         * @brief Adds a colored line to the debug geometry.
         * 
         * @param start The start position of the line in world coordinates.
         * @param end The end position of the line in world coordinates.
         * @param color The color of the line as ARGB (default: white).
         */
        virtual void AddLine(const Vector3& start, const Vector3& end, uint32 color = 0xffffffff) = 0;

        /**
         * @brief Adds a colored triangle to the debug geometry.
         * 
         * @param v1 First vertex of the triangle in world coordinates.
         * @param v2 Second vertex of the triangle in world coordinates.
         * @param v3 Third vertex of the triangle in world coordinates.
         * @param color The color of the triangle as ARGB (default: white).
         */
        virtual void AddTriangle(const Vector3& v1, const Vector3& v2, const Vector3& v3, uint32 color = 0xffffffff) = 0;

        /**
         * @brief Clears all debug geometry.
         * 
         * Removes all lines and triangles that have been added to this debug geometry instance.
         */
        virtual void Clear() = 0;

        virtual void Finish() = 0;

        /**
         * @brief Sets the visibility of the debug geometry.
         * 
         * @param visible True to show the debug geometry, false to hide it.
         */
        virtual void SetVisible(bool visible) = 0;

        /**
         * @brief Gets the current visibility state of the debug geometry.
         * 
         * @return True if the debug geometry is visible, false otherwise.
         */
        virtual bool IsVisible() const = 0;
    };
}
