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
