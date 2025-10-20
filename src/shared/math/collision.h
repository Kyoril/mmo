#pragma once

#include "aabb.h"
#include "vector3.h"

#include <vector>

namespace mmo
{
    struct Capsule;

    float ClosestPointOnSegment(const Vector3& segA, const Vector3& segB, const Vector3& point, Vector3& outPoint);

    float ClosestPointOnTriangle(const Vector3& point, const Vector3& a, const Vector3& b, const Vector3& c, Vector3& outPoint);

    float ClosestSegmentSegment(const Vector3& p1, const Vector3& q1, const Vector3& p2, const Vector3& q2, Vector3& outSegmentPoint1, Vector3& outSegmentPoint2);


	// Project vector onto plane defined by normal
	inline Vector3 ProjectVectorOntoPlane(const Vector3& vector, const Vector3& normal)
	{
		return vector - normal * vector.Dot(normal);
	}

    // Project movement considering multiple surfaces (original function for walkable surfaces)
    Vector3 ProjectMovementAlongSurfaces(const Vector3& movement, const std::vector<Vector3>& normals);

    /// Determines whether a point is inside a triangle in 3D space.
	///	@param p The point to test.
	///	@param a The first point of the triangle.
	///	@param b The second point of the triangle.
	///	@param c The third point of the triangle.
	///	@param barycentricCoords Output parameter which contains the barycentric coordinates of the point.
	///	@returns True if the point is inside the triangle, false otherwise.
    bool PointInTriangle(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c, Vector3& barycentricCoords);

    /// Computes the closest points between a line segment and a triangle in 3D space.
	/// @param segA The first point of the line segment.
	/// @param segB The second point of the line segment.
	/// @param triA The first point of the triangle.
	/// @param triB The second point of the triangle.
	/// @param triC The third point of the triangle.
	/// @param closestPointSegment Output parameter which contains the closest point on the line segment.
	/// @param closestPointTriangle Output parameter which contains the closest point on the triangle.
	/// @returns The squared distance between the closest points.
    float ClosestSegmentTriangle(
        const Vector3& segA,
        const Vector3& segB,
        const Vector3& triA,
        const Vector3& triB,
        const Vector3& triC,
        Vector3& closestPointSegment,
        Vector3& closestPointTriangle
    );

    /// Determines if a capsule intersects with a triangle.
	/// @param capsule The capsule to test.
	/// @param a The first point of the triangle to test.
	///	@param b The second point of the triangle to test.
	///	@param c The third point of the triangle to test.
	///	@param collisionPoint Output parameter which contains the point of collision.
	///	@param collisionNormal Output parameter which contains the normal of the collision.
	///	@param penetrationDepth Output parameter which contains the penetration depth.
	///	@returns True if the capsule intersects with the triangle, false otherwise.
    bool CapsuleTriangleIntersection(const Capsule& capsule, const Vector3& a, const Vector3& b, const Vector3& c, Vector3& collisionPoint, Vector3& collisionNormal, float& penetrationDepth, float& distance);
}
