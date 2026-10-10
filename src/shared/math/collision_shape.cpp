// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "collision_shape.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		constexpr float MinScale = 0.001f;

		/// Collects local-space triangles, winding each so its normal agrees with an outward hint.
		class LocalTriangles final
		{
		public:
			void AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& outward)
			{
				const Vector3 normal = (b - a).Cross(c - a);
				if (normal.Dot(outward) >= 0.0f)
				{
					Push(a, b, c);
				}
				else
				{
					Push(a, c, b);
				}
			}

			/// a, b, c, d in perimeter order.
			void AddQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Vector3& outward)
			{
				// Decide the winding once from the whole quad so both halves agree.
				const Vector3 normal = (b - a).Cross(c - a) + (c - a).Cross(d - a);
				if (normal.Dot(outward) >= 0.0f)
				{
					Push(a, b, c);
					Push(a, c, d);
				}
				else
				{
					Push(a, c, b);
					Push(a, d, c);
				}
			}

			void AppendTransformed(const Matrix4& transform, std::vector<Vector3>& vertices, std::vector<uint32>& indices) const
			{
				const uint32 base = static_cast<uint32>(vertices.size());
				for (const auto& v : m_vertices)
				{
					vertices.push_back(transform.TransformAffine(v));
				}
				for (const uint32 i : m_indices)
				{
					indices.push_back(base + i);
				}
			}

		private:
			void Push(const Vector3& a, const Vector3& b, const Vector3& c)
			{
				const uint32 base = static_cast<uint32>(m_vertices.size());
				m_vertices.push_back(a);
				m_vertices.push_back(b);
				m_vertices.push_back(c);
				m_indices.push_back(base);
				m_indices.push_back(base + 1);
				m_indices.push_back(base + 2);
			}

			std::vector<Vector3> m_vertices;
			std::vector<uint32> m_indices;
		};

		void BuildBox(LocalTriangles& out)
		{
			const float h = 0.5f;
			const Vector3 p000(-h, -h, -h), p100(h, -h, -h), p110(h, h, -h), p010(-h, h, -h);
			const Vector3 p001(-h, -h, h), p101(h, -h, h), p111(h, h, h), p011(-h, h, h);

			out.AddQuad(p100, p101, p111, p110, Vector3(1, 0, 0));
			out.AddQuad(p000, p010, p011, p001, Vector3(-1, 0, 0));
			out.AddQuad(p010, p110, p111, p011, Vector3(0, 1, 0));
			out.AddQuad(p000, p001, p101, p100, Vector3(0, -1, 0));
			out.AddQuad(p001, p011, p111, p101, Vector3(0, 0, 1));
			out.AddQuad(p000, p100, p110, p010, Vector3(0, 0, -1));
		}

		void BuildWedge(LocalTriangles& out)
		{
			const float h = 0.5f;
			const Vector3 b0(-h, -h, -h), b1(h, -h, -h), b2(h, -h, h), b3(-h, -h, h);
			const Vector3 t2(h, h, h), t3(-h, h, h);

			out.AddQuad(b0, b1, b2, b3, Vector3(0, -1, 0));     // bottom
			out.AddQuad(b3, b2, t2, t3, Vector3(0, 0, 1));      // high back face
			out.AddQuad(b0, t3, t2, b1, Vector3(0, 1, -1));     // slope
			out.AddTriangle(b1, b2, t2, Vector3(1, 0, 0));      // right side
			out.AddTriangle(b0, t3, b3, Vector3(-1, 0, 0));     // left side
		}

		void BuildPlane(LocalTriangles& out, const bool twoSided)
		{
			const float h = 0.5f;
			const Vector3 a(-h, 0, -h), b(h, 0, -h), c(h, 0, h), d(-h, 0, h);
			out.AddQuad(a, b, c, d, Vector3(0, 1, 0));
			if (twoSided)
			{
				out.AddQuad(a, b, c, d, Vector3(0, -1, 0));
			}
		}

		constexpr float FullTurn = 6.28318530718f;

		/// Unit-circle point for angle theta, turning counter-clockwise seen from above (north = -Z)
		/// unless `clockwise`.
		Vector3 HelixDirection(const float theta, const bool clockwise)
		{
			const float side = clockwise ? -1.0f : 1.0f;
			return Vector3(std::cos(theta), 0.0f, -side * std::sin(theta));
		}

		void BuildCylinder(LocalTriangles& out, const uint16 segments)
		{
			const float r = 0.5f;
			const Vector3 top(0, 0.5f, 0), bottom(0, -0.5f, 0);
			for (uint16 i = 0; i < segments; ++i)
			{
				const float a0 = FullTurn * i / segments;
				const float a1 = FullTurn * (i + 1) / segments;
				const Vector3 d0(std::cos(a0), 0, std::sin(a0)), d1(std::cos(a1), 0, std::sin(a1));
				const Vector3 b0 = d0 * r + bottom, b1 = d1 * r + bottom;
				const Vector3 t0 = d0 * r + top, t1 = d1 * r + top;
				const Vector3 outward = (d0 + d1) * 0.5f;

				out.AddQuad(b0, b1, t1, t0, outward);
				out.AddTriangle(top, t0, t1, Vector3(0, 1, 0));
				out.AddTriangle(bottom, b0, b1, Vector3(0, -1, 0));
			}
		}

		void BuildHelix(LocalTriangles& out, const CollisionShape& s)
		{
			const float outer = 0.5f;
			const float inner = 0.5f * s.innerRadius;
			const float sweep = s.sweepDegrees * FullTurn / 360.0f;
			const uint16 n = s.segments;

			auto topY = [&](const uint16 i) { return -0.5f + static_cast<float>(i) / n; };
			auto point = [&](const uint16 i, const float radius, const float y)
			{
				const Vector3 d = HelixDirection(sweep * i / n, s.clockwise);
				return Vector3(d.x * radius, y, d.z * radius);
			};

			for (uint16 i = 0; i < n; ++i)
			{
				const uint16 j = i + 1;
				const Vector3 ti0 = point(i, inner, topY(i)), to0 = point(i, outer, topY(i));
				const Vector3 ti1 = point(j, inner, topY(j)), to1 = point(j, outer, topY(j));
				const Vector3 bi0 = point(i, inner, topY(i) - s.thickness), bo0 = point(i, outer, topY(i) - s.thickness);
				const Vector3 bi1 = point(j, inner, topY(j) - s.thickness), bo1 = point(j, outer, topY(j) - s.thickness);
				const Vector3 radial = HelixDirection(sweep * (i + 0.5f) / n, s.clockwise);

				out.AddQuad(ti0, to0, to1, ti1, Vector3(0, 1, 0));    // tread
				out.AddQuad(bi0, bi1, bo1, bo0, Vector3(0, -1, 0));   // underside
				out.AddQuad(bo0, bo1, to1, to0, radial);              // outer wall
				out.AddQuad(bi0, ti0, ti1, bi1, -radial);             // inner wall
			}

			// End caps face against (start) and along (end) the direction of travel. The travel
			// direction is d/dtheta of HelixDirection: (-sin t, 0, -side * cos t).
			const float side = s.clockwise ? -1.0f : 1.0f;
			const Vector3 startTangent(0.0f, 0.0f, side);
			const Vector3 endTangent(-std::sin(sweep), 0.0f, -side * std::cos(sweep));

			out.AddQuad(point(0, inner, topY(0) - s.thickness), point(0, outer, topY(0) - s.thickness),
				point(0, outer, topY(0)), point(0, inner, topY(0)), startTangent);
			out.AddQuad(point(n, inner, topY(n) - s.thickness), point(n, outer, topY(n) - s.thickness),
				point(n, outer, topY(n)), point(n, inner, topY(n)), endTangent);
		}

		Vector3 ToLocal(const CollisionShape& shape, const Vector3& point)
		{
			const Vector3 rotated = shape.rotation.UnitInverse() * (point - shape.position);
			return Vector3(rotated.x / shape.scale.x, rotated.y / shape.scale.y, rotated.z / shape.scale.z);
		}
	}

	CollisionShape SanitizeCollisionShape(const CollisionShape& shape)
	{
		CollisionShape sane = shape;
		if (sane.type >= collision_shape_type::Count_)
		{
			sane.type = collision_shape_type::Box;
		}

		sane.scale = Vector3(
			std::max(std::abs(shape.scale.x), MinScale),
			std::max(std::abs(shape.scale.y), MinScale),
			std::max(std::abs(shape.scale.z), MinScale));

		sane.rotation.Normalize();
		sane.segments = static_cast<uint16>(std::clamp<int>(shape.segments, 3, 256));
		sane.innerRadius = std::clamp(shape.innerRadius, 0.05f, 0.95f);
		sane.sweepDegrees = std::clamp(shape.sweepDegrees, 1.0f, 3600.0f);
		sane.thickness = std::clamp(shape.thickness, 0.001f, 1.0f);

		if (!CollisionShapeSupportsCut(sane.type))
		{
			sane.op = collision_shape_op::Add;
		}

		return sane;
	}

	bool CollisionShapeSupportsCut(const collision_shape_type::Type type)
	{
		return type != collision_shape_type::Plane;
	}

	const char* GetCollisionShapeTypeName(const collision_shape_type::Type type)
	{
		switch (type)
		{
		case collision_shape_type::Box:
			return "Box";
		case collision_shape_type::Wedge:
			return "Wedge";
		case collision_shape_type::Cylinder:
			return "Cylinder";
		case collision_shape_type::HelixRamp:
			return "Helix Ramp";
		case collision_shape_type::Plane:
			return "Plane";
		default:
			return "Unknown";
		}
	}

	Matrix4 GetCollisionShapeTransform(const CollisionShape& shape)
	{
		const CollisionShape sane = SanitizeCollisionShape(shape);
		Matrix4 transform;
		transform.MakeTransform(sane.position, sane.scale, sane.rotation);
		return transform;
	}

	void TessellateCollisionShape(const CollisionShape& shape, std::vector<Vector3>& vertices, std::vector<uint32>& indices)
	{
		const CollisionShape sane = SanitizeCollisionShape(shape);

		LocalTriangles local;
		switch (sane.type)
		{
		case collision_shape_type::Box:
			BuildBox(local);
			break;
		case collision_shape_type::Wedge:
			BuildWedge(local);
			break;
		case collision_shape_type::Cylinder:
			BuildCylinder(local, sane.segments);
			break;
		case collision_shape_type::HelixRamp:
			BuildHelix(local, sane);
			break;
		case collision_shape_type::Plane:
			BuildPlane(local, sane.twoSided);
			break;
		default:
			break;
		}

		local.AppendTransformed(GetCollisionShapeTransform(sane), vertices, indices);
	}

	bool IsPointInsideCollisionShape(const CollisionShape& shape, const Vector3& point)
	{
		const CollisionShape sane = SanitizeCollisionShape(shape);
		const Vector3 p = ToLocal(sane, point);
		const float h = 0.5f;

		switch (sane.type)
		{
		case collision_shape_type::Box:
			return std::abs(p.x) <= h && std::abs(p.y) <= h && std::abs(p.z) <= h;
		case collision_shape_type::Wedge:
			return std::abs(p.x) <= h && p.y >= -h && p.z <= h && p.y <= p.z;
		case collision_shape_type::Cylinder:
			return std::abs(p.y) <= h && p.x * p.x + p.z * p.z <= h * h;
		case collision_shape_type::HelixRamp:
		{
			const float radiusSq = p.x * p.x + p.z * p.z;
			const float inner = h * sane.innerRadius;
			if (radiusSq < inner * inner || radiusSq > h * h)
			{
				return false;
			}

			const float side = sane.clockwise ? -1.0f : 1.0f;
			float phi = std::atan2(-side * p.z, p.x);
			if (phi < 0.0f)
			{
				phi += FullTurn;
			}

			// The band passes this angle once per turn; check each pass.
			const float sweep = sane.sweepDegrees * FullTurn / 360.0f;
			for (float theta = phi; theta <= sweep; theta += FullTurn)
			{
				const float top = -h + theta / sweep;
				if (p.y <= top && p.y >= top - sane.thickness)
				{
					return true;
				}
			}
			return false;
		}
		default:
			return false;
		}
	}

	bool IsCollisionFaceWalkable(const Vector3& a, const Vector3& b, const Vector3& c)
	{
		const Vector3 normal = (b - a).Cross(c - a);
		const float length = normal.GetLength();
		if (length < 1.0e-6f)
		{
			return false;
		}

		return std::abs(normal.y / length) >= CollisionWalkableFloorY;
	}
}
