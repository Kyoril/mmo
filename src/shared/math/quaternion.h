
#pragma once

#include "vector3.h"

#include "base/macros.h"

#include <cstring>
#include <utility>

namespace mmo
{
	class Quaternion final
	{
	public:
		inline Quaternion()
			: w(1), x(0), y(0), z(0)
		{
		}
		inline Quaternion(float w, float x, float y, float z)
			: w(w), x(x), y(y), z(z)
		{
		}

		/*inline Quaternion(const Matrix3& rot)
		{
			FromRotationMatrix(rot);
		}*/

		inline Quaternion(float angle, const Vector3& axis)
		{
			FromAngleAxis(angle, axis);
		}

		inline Quaternion(const Vector3& xaxis, const Vector3& yaxis, const Vector3& zaxis)
		{
			FromAxes(xaxis, yaxis, zaxis);
		}

		inline Quaternion(const Vector3* axis)
		{
			FromAxes(axis);
		}

		inline Quaternion(float* arr)
		{
			std::memcpy(&w, arr, sizeof(float) * 4);
		}

	public:
		inline void swap(Quaternion& other)
		{
			std::swap(w, other.w);
			std::swap(x, other.x);
			std::swap(y, other.y);
			std::swap(z, other.z);
		}

	public:
		inline float operator [] (const size_t i) const
		{
			ASSERT(i < 4);
			return *(&w + i);
		}
		inline float& operator [] (const size_t i)
		{
			ASSERT(i < 4);
			return *(&w + i);
		}
		inline float* Ptr()
		{
			return &w;
		}
		inline const float* Ptr() const
		{
			return &w;
		}

		//void FromRotationMatrix(const Matrix3& rot);
		//void ToRotationMatrix(Matrix3& rot) const;

		void FromAngleAxis(float angle, const Vector3& axis);
		void ToAngleAxis(float& angle, Vector3& axis) const;
		void FromAxes(const Vector3* axis);
		void FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis);
		void ToAxes(Vector3* axis) const;
		void ToAxes(Vector3& xAxis, Vector3& yAxis, Vector3& zAxis) const;

		Vector3 GetXAxis() const;
		Vector3 GetYAxis() const;
		Vector3 GetZAxis() const;

		inline Quaternion& operator= (const Quaternion& q)
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
		inline bool operator== (const Quaternion& rhs) const
		{
			return 
				(rhs.x == x) && (rhs.y == y) &&
				(rhs.z == z) && (rhs.w == w);
		}
		inline bool operator!= (const Quaternion& rhs) const
		{
			return !operator==(rhs);
		}

		float Dot(const Quaternion& q) const;
		float Norm() const;
		float Normalize();
		Quaternion Inverse() const;
		Quaternion UnitInverse() const;
		Quaternion Exp() const;
		Quaternion Log() const;
		Vector3 operator* (const Vector3& v) const;

		float GetRoll(bool reprojectAxis = true) const;
		float GetPitch(bool reprojectAxis = true) const;
		float GetYaw(bool reprojectAxis = true) const;

		bool Equals(const Quaternion& rhs, float tolerance) const;

		inline bool OrientationEquals(const Quaternion& other, float tolerance = 1e-3) const
		{
			float d = this->Dot(other);
			return 1 - d * d < tolerance;
		}

		static Quaternion Slerp(float t, const Quaternion& p, const Quaternion& q, bool shortestPath = false);
		static Quaternion SlerpExtraSpins(float t, const Quaternion& p, const Quaternion& q, int extraSpins);
		static void Intermediate(const Quaternion& q0, const Quaternion& q1, const Quaternion& q2, Quaternion& a, Quaternion& b);
		static Quaternion Squad(float t, const Quaternion& p, const Quaternion& a, const Quaternion& b, const Quaternion& q, bool shortestPath = false);
		static Quaternion NLerp(float t, const Quaternion& p, const Quaternion& q, bool shortestPath = false);

		/// Cutoff for sine near zero
		static const float Epsilon;

		// special values
		static const Quaternion ZERO;
		static const Quaternion IDENTITY;

	public:
		float w, x, y, z;

		/// Check whether this quaternion contains valid values
		inline bool IsNaN() const
		{
			return false;
			//return Math::isNaN(x) || Math::isNaN(y) || Math::isNaN(z) || Math::isNaN(w);
		}

		friend std::ostream &operator<<(std::ostream &o, const Quaternion &q);
	};
}