// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "vector3.h"

#include "base/macros.h"

#include <cstring>
#include <utility>

#include "degree.h"

namespace mmo
{
	class Radian;
	class Matrix3;

	struct Rotator
	{
		Degree roll, yaw, pitch;
	};
	
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
			: w(0), x(0), y(0), z(0)
		{
			std::memcpy(&w, arr, sizeof(float) * 4);
		}

	public:

		float ClampAxis(float Angle) const
		{
			// returns Angle in the range (-360,360)
			Angle = fmod(Angle, 360.0f);

			if (Angle < 0.0f)
			{
				// shift to [0,360) range
				Angle += 360.0f;
			}

			return Angle;
		}

		float NormalizeAxis(float Angle) const
		{
			// returns Angle in the range [0,360)
			Angle = ClampAxis(Angle);

			if (Angle > 180.0f)
			{
				// shift to (-180,180]
				Angle -= 360.0f;
			}

			return Angle;
		}

		Rotator ToRotator() const
		{
			const float SingularityTest = z * x - w * y;
			const float YawY = 2.f * (w * z + y * y);
			const float YawX = (1.f - 2.f * (y * y + z * z));

			const float SINGULARITY_THRESHOLD = 0.4999995f;
			const float RAD_TO_DEG = (180.f / Pi);
			float Pitch, Yaw, Roll;

			if (SingularityTest < -SINGULARITY_THRESHOLD)
			{
				Pitch = -90.f;
				Yaw = (atan2(YawY, YawX) * RAD_TO_DEG);
				Roll = NormalizeAxis(-Yaw - (2.f * atan2(x, w) * RAD_TO_DEG));
			}
			else if (SingularityTest > SINGULARITY_THRESHOLD)
			{
				Pitch = 90.f;
				Yaw = (atan2(YawY, YawX) * RAD_TO_DEG);
				Roll = NormalizeAxis(Yaw - (2.f * atan2(x, w) * RAD_TO_DEG));
			}
			else
			{
				Pitch = (asin(2.f * SingularityTest) * RAD_TO_DEG);
				Yaw = (atan2(YawY, YawX) * RAD_TO_DEG);
				Roll = (atan2(-2.f * (w * x + y * z), (1.f - 2.f * (x * x + y * y))) * RAD_TO_DEG);
			}

			return { Degree(Roll), Degree(Yaw), Degree(Pitch) };
		}

		static inline void SinCos(float* ScalarSin, float* ScalarCos, float Value)
		{
			*ScalarSin = sin(Value);
			*ScalarCos = cos(Value);
		}

		static Quaternion FromRotator(const Rotator& rotator)
		{
			const float DEG_TO_RAD = Pi / (180.f);
			const float RADS_DIVIDED_BY_2 = DEG_TO_RAD / 2.f;
			float SP, SY, SR;
			float CP, CY, CR;

			const float PitchNoWinding = fmod(rotator.pitch.GetValueDegrees(), 360.0f);
			const float YawNoWinding = fmod(rotator.yaw.GetValueDegrees(), 360.0f);
			const float RollNoWinding = fmod(rotator.roll.GetValueDegrees(), 360.0f);

			SinCos(&SP, &CP, PitchNoWinding * RADS_DIVIDED_BY_2);
			SinCos(&SY, &CY, YawNoWinding * RADS_DIVIDED_BY_2);
			SinCos(&SR, &CR, RollNoWinding * RADS_DIVIDED_BY_2);

			Quaternion RotationQuat;
			RotationQuat.x = CR * SP * SY - SR * CP * CY;
			RotationQuat.y = -CR * SP * CY - SR * CP * SY;
			RotationQuat.z = CR * CP * SY - SR * SP * CY;
			RotationQuat.w = CR * CP * CY + SR * SP * SY;

			return RotationQuat;
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
