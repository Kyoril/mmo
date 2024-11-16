
#include "collision.h"

#include <cmath>
#include <limits>
#include <algorithm>

#include "capsule.h"

namespace mmo
{
	float ClosestPointOnSegment(const Vector3& segA, const Vector3& segB, const Vector3& point, Vector3& outPoint)
	{
		const Vector3 ab = segB - segA;
		const float abLengthSquared = ab.Dot(ab);

        if (abLengthSquared == 0.0f) 
        {
            // Segment is a point
            outPoint = segA;
            return (point - segA).GetSquaredLength();
        }

        float t = (point - segA).Dot(ab) / abLengthSquared;
        t = std::clamp(t, 0.0f, 1.0f);
        outPoint = segA + ab * t;

        return (point - outPoint).GetSquaredLength();
	}

	float ClosestPointOnTriangle(const Vector3& point, const Vector3& a, const Vector3& b, const Vector3& c, Vector3& outPoint)
	{
        // Compute vectors
        const Vector3 ab = b - a;
        const Vector3 ac = c - a;
        const Vector3 ap = point - a;

        const float d1 = ab.Dot(ap);
        const float d2 = ac.Dot(ap);

        // Check if point is in vertex region outside A
        if (d1 <= 0.0f && d2 <= 0.0f) 
        {
            outPoint = a;
            return (point - a).GetSquaredLength();
        }

        // Check if point is in vertex region outside B
        const Vector3 bp = point - b;
        const float d3 = ab.Dot(bp);
        const float d4 = ac.Dot(bp);
        if (d3 >= 0.0f && d4 <= d3)
        {
            outPoint = b;
            return (point - b).GetSquaredLength();
        }

        // Check if point is in edge region of AB
        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) 
        {
	        const float v = d1 / (d1 - d3);
            outPoint = a + ab * v;
            return (point - outPoint).GetSquaredLength();
        }

        // Check if point is in vertex region outside C
        const Vector3 cp = point - c;
        const float d5 = ab.Dot(cp);
        const float d6 = ac.Dot(cp);
        if (d6 >= 0.0f && d5 <= d6) 
        {
            outPoint = c;
            return (point - c).GetSquaredLength();
        }

        // Check if point is in edge region of AC
        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
	        const float w = d2 / (d2 - d6);
            outPoint = a + ac * w;
            return (point - outPoint).GetSquaredLength();
        }

        // Check if point is in edge region of BC
        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
	        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            outPoint = b + (c - b) * w;
            return (point - outPoint).GetSquaredLength();
        }

        // Point is inside face region
        const float denom = 1.0f / (va + vb + vc);
        const float v = vb * denom;
        const float w = vc * denom;
        outPoint = a + ab * v + ac * w;
        return (point - outPoint).GetSquaredLength();
	}

	float ClosestSegmentSegment(const Vector3& p1, const Vector3& q1, const Vector3& p2, const Vector3& q2, Vector3& outSegmentPoint1, Vector3& outSegmentPoint2)
	{
		const Vector3 d1 = q1 - p1; // Direction vector of segment S1
		const Vector3 d2 = q2 - p2; // Direction vector of segment S2
		const Vector3 r = p1 - p2;
		const float a = d1.Dot(d1); // Squared length of segment S1
		const float e = d2.Dot(d2); // Squared length of segment S2
		const float f = d2.Dot(r);

        float s, t;
        if (a <= std::numeric_limits<float>::epsilon() && e <= std::numeric_limits<float>::epsilon()) 
        {
            // Both segments are points
            s = t = 0.0f;
            outSegmentPoint1 = p1;
            outSegmentPoint2 = p2;
            return (outSegmentPoint1 - outSegmentPoint2).GetSquaredLength();
        }

        if (a <= std::numeric_limits<float>::epsilon())
        {
            // First segment is a point
            s = 0.0f;
            t = f / e;
            t = std::clamp(t, 0.0f, 1.0f);
        }
        else 
        {
	        const float c = d1.Dot(r);
            if (e <= std::numeric_limits<float>::epsilon()) 
            {
                // Second segment is a point
                t = 0.0f;
                s = -c / a;
                s = std::clamp(s, 0.0f, 1.0f);
            }
            else
            {
                // The general case
                const float b = d1.Dot(d2);
                const float denom = a * e - b * b;

                if (denom != 0.0f) 
                {
                    s = (b * f - c * e) / denom;
                    s = std::clamp(s, 0.0f, 1.0f);
                }
                else
                {
                    s = 0.0f;
                }

                t = (b * s + f) / e;

                if (t < 0.0f)
                {
                    t = 0.0f;
                    s = -c / a;
                    s = std::clamp(s, 0.0f, 1.0f);
                }
                else if (t > 1.0f)
                {
                    t = 1.0f;
                    s = (b - c) / a;
                    s = std::clamp(s, 0.0f, 1.0f);
                }
            }
        }

        outSegmentPoint1 = p1 + d1 * s;
        outSegmentPoint2 = p2 + d2 * t;
        return (outSegmentPoint1 - outSegmentPoint2).GetSquaredLength();
	}

	bool PointInTriangle(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c, Vector3& barycentricCoords)
	{
        // Compute vectors
        const Vector3 v0 = c - a;
        const Vector3 v1 = b - a;
        const Vector3 v2 = p - a;

        // Compute dot products
        const float dot00 = v0.Dot(v0);
        const float dot01 = v0.Dot(v1);
        const float dot02 = v0.Dot(v2);
        const float dot11 = v1.Dot(v1);
        const float dot12 = v1.Dot(v2);

        // Compute barycentric coordinates
        const float denom = dot00 * dot11 - dot01 * dot01;
        if (denom == 0.0f) {
            // Triangle is degenerate
            return false;
        }
        const float invDenom = 1.0f / denom;
        const float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        const float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

        // Check if point is in triangle
        if (u >= 0.0f && v >= 0.0f && (u + v) <= 1.0f)
        {
            barycentricCoords = Vector3(1.0f - u - v, v, u);
            return true;
        }

        return false;
	}

	float ClosestSegmentTriangle(const Vector3& segA, const Vector3& segB, const Vector3& triA, const Vector3& triB, const Vector3& triC, Vector3& closestPointSegment, Vector3& closestPointTriangle)
	{
        // Variables to hold temporary closest points and distances
        Vector3 tempClosestSegment, tempClosestTriangle;
        float minDistSquared = std::numeric_limits<float>::max();

        // First, check if the segment intersects the triangle
        // Compute the plane of the triangle
        const Vector3 edge0 = triB - triA;
        const Vector3 edge1 = triC - triA;
        Vector3 normal = edge0.Cross(edge1);

        const float normalLength = normal.GetLength();
        if (normalLength == 0.0f) 
        {
            // Triangle is degenerate
            return minDistSquared;
        }

        normal = normal / normalLength;

        // Compute distances of segment endpoints to the plane
        const float distA = (segA - triA).Dot(normal);
        const float distB = (segB - triA).Dot(normal);

        // If the segment crosses the plane
        if (distA * distB <= 0.f) 
        {
            // Line segment intersects the plane of the triangle
            // Find intersection point
            const float t = distA / (distA - distB);
            const Vector3 pointOnPlane = segA + (segB - segA) * t;

            // Check if the intersection point is inside the triangle
            Vector3 barycentricCoords;
            if (bool inside = PointInTriangle(pointOnPlane, triA, triB, triC, barycentricCoords))
            {
                // Closest point on segment is pointOnPlane
                // Closest point on triangle is pointOnPlane
                closestPointSegment = pointOnPlane;
                closestPointTriangle = pointOnPlane;
                return 0.f; // Distance squared is zero
            }
        }

        // If no intersection, find closest points between segment and triangle edges

        // Test segment against triangle edges
        struct Edge {
            Vector3 a;
            Vector3 b;
        } edges[3] = 
        {
            { triA, triB },
            { triB, triC },
            { triC, triA }
        };

        for (auto& edge : edges)
        {
            Vector3 cpSeg, cpEdge;
            const float sqDist = ClosestSegmentSegment(segA, segB, edge.a, edge.b, cpSeg, cpEdge);
            if (sqDist < minDistSquared)
            {
                minDistSquared = sqDist;
                closestPointSegment = cpSeg;
                closestPointTriangle = cpEdge;
            }
        }

        // Test segment endpoints against triangle face
        {
            Vector3 cpTriangle;
            const float sqDistA = ClosestPointOnTriangle(segA, triA, triB, triC, cpTriangle);
            if (sqDistA < minDistSquared) 
            {
                minDistSquared = sqDistA;
                closestPointSegment = segA;
                closestPointTriangle = cpTriangle;
            }

            const float sqDistB = ClosestPointOnTriangle(segB, triA, triB, triC, cpTriangle);
            if (sqDistB < minDistSquared) 
            {
                minDistSquared = sqDistB;
                closestPointSegment = segB;
                closestPointTriangle = cpTriangle;
            }
        }

        // Test triangle vertices against segment
        {
            Vector3 cpSeg;
            float sqDist = ClosestPointOnSegment(segA, segB, triA, cpSeg);
            if (sqDist < minDistSquared) 
            {
                minDistSquared = sqDist;
                closestPointSegment = cpSeg;
                closestPointTriangle = triA;
            }

            sqDist = ClosestPointOnSegment(segA, segB, triB, cpSeg);
            if (sqDist < minDistSquared) 
            {
                minDistSquared = sqDist;
                closestPointSegment = cpSeg;
                closestPointTriangle = triB;
            }

            sqDist = ClosestPointOnSegment(segA, segB, triC, cpSeg);
            if (sqDist < minDistSquared) 
            {
                minDistSquared = sqDist;
                closestPointSegment = cpSeg;
                closestPointTriangle = triC;
            }
        }

        return minDistSquared;
	}

	bool CapsuleTriangleIntersection(const Capsule& capsule, const Vector3& a, const Vector3& b, const Vector3& c, Vector3& collisionPoint,
		Vector3& collisionNormal, float& penetrationDepth)
	{
		Vector3 closestPointCapsule, closestPointTriangle;

		if (const float sqDistance = ClosestSegmentTriangle(
			capsule.pointA, capsule.pointB,
			a, b, c,
			closestPointCapsule, closestPointTriangle
		); sqDistance <= capsule.radius * capsule.radius)
		{
			const float distance = sqrtf(sqDistance);
			penetrationDepth = capsule.radius - distance;
			collisionNormal = (closestPointCapsule - closestPointTriangle).NormalizedCopy();
			collisionPoint = closestPointCapsule - collisionNormal * (capsule.radius - penetrationDepth);
			return true;
		}

		return false;
	}

	AABB CapsuleToAABB(const Capsule& capsule)
	{
		Vector3 minPoint = TakeMinimum(capsule.pointA, capsule.pointB) - Vector3(capsule.radius, capsule.radius, capsule.radius);
		Vector3 maxPoint = TakeMaximum(capsule.pointA, capsule.pointB) + Vector3(capsule.radius, capsule.radius, capsule.radius);
        return { minPoint, maxPoint };
	}
}
