
#pragma once

#include "vector3.h"

#include "base/macros.h"

#include <cstring>
#include <utility>

namespace mmo
{
	class Radian;
	class Matrix3;
	
	
	class Quaternion final
	{
	public:
		Quaternion()
			: w(1), x(0), y(0), z(0)
		{
		}
		Quaternion(float w, float x, float y, float z)
			: w(w), x(x), y(y), z(z)
		{
		}

		Quaternion(const Matrix3& rot)
		{
			FromRotationMatrix(rot);
		}

		Quaternion(const Radian& angle, const Vector3& axis)
		{
			FromAngleAxis(angle, axis);
		}

		Quaternion(const Vector3& xaxis, const Vector3& yaxis, const Vector3& zaxis)
		{
			FromAxes(xaxis, yaxis, zaxis);
		}

		Quaternion(const Vector3* axis)
		{
			FromAxes(axis);
		}

		Quaternion(float* arr)
		{
			std::memcpy(&w, arr, sizeof(float) * 4);
		}

	public:
		void swap(Quaternion& other)
		{
			std::swap(w, other.w);
			std::swap(x, other.x);
			std::swap(y, other.y);
			std::swap(z, other.z);
		}

	public:
		float operator [] (const size_t i) const
		{
			ASSERT(i < 4);
			return *(&w + i);
		}
		
		float& operator [] (const size_t i)
		{
			ASSERT(i < 4);
			return *(&w + i);
		}
		
		float* Ptr()
		{
			return &w;
		}

		[[nodiscard]] const float* Ptr() const
		{
			return &w;
		}

		void FromRotationMatrix(const Matrix3& kRot);
		void ToRotationMatrix(Matrix3& kRot) const;

		void FromAngleAxis(const Radian& angle, const Vector3& axis);
		void ToAngleAxis(Radian& angle, Vector3& axis) const;
		void FromAxes(const Vector3* axis);
		void FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis);
		void ToAxes(Vector3* axis) const;
		void ToAxes(Vector3& xAxis, Vector3& yAxis, Vector3& zAxis) const;

		Vector3 GetXAxis() const;
		Vector3 GetYAxis() const;
		Vector3 GetZAxis() const;

		Quaternion& operator= (const Quaternion& q)
		{
			w = q.w;
			x = q.x;
			y = q.y;
			z = q.z;
			return *this;
		}
		
		Quaternion operator+ (const Quaternion& q) const;
		Quaternion operator- (const Quaternion& q) const;
		Quaternion operator* (const Quaternion& q) const;
		Quaternion operator* (float s) const;
		friend Quaternion operator* (float s, const Quaternion& q);
		Quaternion operator-() const;

		bool operator== (const Quaternion& rhs) const
		{
			return 
				(rhs.x == x) && (rhs.y == y) &&
				(rhs.z == z) && (rhs.w == w);
		}
		
		bool operator!= (const Quaternion& rhs) const
		{
			return !operator==(rhs);
		}

		[[nodiscard]] float Dot(const Quaternion& q) const;
		[[nodiscard]] float Norm() const;
		float Normalize();
		[[nodiscard]] Quaternion Inverse() const;
		[[nodiscard]] Quaternion UnitInverse() const;
		[[nodiscard]] Quaternion Exp() const;
		[[nodiscard]] Quaternion Log() const;
		Vector3 operator* (const Vector3& v) const;

		[[nodiscard]] Radian GetRoll(bool reprojectAxis = true) const;
		[[nodiscard]] Radian GetPitch(bool reprojectAxis = true) const;
		[[nodiscard]] Radian GetYaw(bool reprojectAxis = true) const;

		[[nodiscard]] bool Equals(const Quaternion& rhs, const Radian& tolerance) const;

		[[nodiscard]] bool OrientationEquals(const Quaternion& other, float tolerance = 1e-3f) const
		{
			float d = this->Dot(other);
			return 1 - d * d < tolerance;
		}

		[[nodiscard]] static Quaternion Slerp(float t, const Quaternion& rkPp, const Quaternion& q, bool shortestPath = false);
		[[nodiscard]] static Quaternion SlerpExtraSpins(float t, const Quaternion& p, const Quaternion& q, int extraSpins);
		static void Intermediate(const Quaternion& q0, const Quaternion& q1, const Quaternion& q2, Quaternion& a, Quaternion& b);
		[[nodiscard]] static Quaternion Squad(float t, const Quaternion& p, const Quaternion& a, const Quaternion& b, const Quaternion& q, bool shortestPath = false);
		[[nodiscard]] static Quaternion NLerp(float t, const Quaternion& p, const Quaternion& q, bool shortestPath = false);

		/// Cutoff for sine near zero
		static const float Epsilon;

		// special values
		static const Quaternion Zero;
		static const Quaternion Identity;

	public:
		float w, x, y, z;

		/// Check whether this quaternion contains valid values
		[[nodiscard]] bool IsNaN() const
		{
			return (x != x) || (y != y) || (z != z) || (w != w);
		}

		friend std::ostream &operator<<(std::ostream &o, const Quaternion &q);
	};

	inline Quaternion operator*(float scalar, const Quaternion& q)
	{
		return Quaternion(scalar * q.w, scalar * q.x, scalar * q.y, scalar * q.z);
	}
}