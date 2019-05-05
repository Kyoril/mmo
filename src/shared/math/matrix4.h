// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "vector3.h"


namespace mmo
{
	/// This class represents a 4x4 matrix used for vector transformations.
	class Matrix4
	{
	public:

		static Matrix4 Identity;

	public:

		union
		{
			struct
			{
				float m11, m12, m13, m14,
					m21, m22, m23, m24,
					m31, m32, m33, m34,
					m41, m42, m43, m44;
			};
			float m[4][4];
			float f[16];
		};

	public:

		explicit Matrix4(float m11 = 1.0f, float m12 = 0.0f, float m13 = 0.0f, float m14 = 0.0f,
			float m21 = 0.0f, float m22 = 1.0f, float m23 = 0.0f, float m24 = 0.0f,
			float m31 = 0.0f, float m32 = 0.0f, float m33 = 1.0f, float m34 = 0.0f,
			float m41 = 0.0f, float m42 = 0.0f, float m43 = 0.0f, float m44 = 1.0f)
		{
			m[0][0] = m11; m[0][1] = m12; m[0][2] = m13; m[0][3] = m14;
			m[1][0] = m21; m[1][1] = m22; m[1][2] = m23; m[1][3] = m24;
			m[2][0] = m31; m[2][1] = m32; m[2][2] = m33; m[2][3] = m34;
			m[3][0] = m41; m[3][1] = m42; m[3][2] = m43; m[3][3] = m44;
		}

	public:

		static Matrix4 MakeTranslation(const Vector3& v)
		{
			return Matrix4(1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				v.x, v.y, v.z, 1.0f);
		}

		static Matrix4 MakeRotationX(float f)
		{
			Matrix4 result;

			result.m[1][1] = result.m[2][2] = cosf(f);
			result.m[1][2] = sinf(f);
			result.m[2][1] = -result.m[1][2];

			return result;
		}

		static Matrix4 MakeRotationY(float f)
		{
			Matrix4 result;

			result.m[0][0] = result.m[2][2] = cosf(f);
			result.m[2][0] = sinf(f);
			result.m[0][2] = -result.m[2][0];

			return result;
		}

		static Matrix4 MakeRotationZ(float f)
		{
			Matrix4 result;

			result.m[0][0] = result.m[1][1] = cosf(f);
			result.m[0][1] = sinf(f);
			result.m[1][0] = -result.m[0][1];

			return result;
		}

		static Matrix4 MakeRotation(float x, float y, float z)
		{
			return MakeRotationZ(z) * MakeRotationX(x) * MakeRotationY(y);
		}

		inline Matrix4 operator*(const Matrix4& b)
		{
			const Matrix4& a = *this;
			return Matrix4(
				b.m11 * a.m11 + b.m21 * a.m12 + b.m31 * a.m13 + b.m41 * a.m14,
				b.m12 * a.m11 + b.m22 * a.m12 + b.m32 * a.m13 + b.m42 * a.m14,
				b.m13 * a.m11 + b.m23 * a.m12 + b.m33 * a.m13 + b.m43 * a.m14,
				b.m14 * a.m11 + b.m24 * a.m12 + b.m34 * a.m13 + b.m44 * a.m14,
				b.m11 * a.m21 + b.m21 * a.m22 + b.m31 * a.m23 + b.m41 * a.m24,
				b.m12 * a.m21 + b.m22 * a.m22 + b.m32 * a.m23 + b.m42 * a.m24,
				b.m13 * a.m21 + b.m23 * a.m22 + b.m33 * a.m23 + b.m43 * a.m24,
				b.m14 * a.m21 + b.m24 * a.m22 + b.m34 * a.m23 + b.m44 * a.m24,
				b.m11 * a.m31 + b.m21 * a.m32 + b.m31 * a.m33 + b.m41 * a.m34,
				b.m12 * a.m31 + b.m22 * a.m32 + b.m32 * a.m33 + b.m42 * a.m34,
				b.m13 * a.m31 + b.m23 * a.m32 + b.m33 * a.m33 + b.m43 * a.m34,
				b.m14 * a.m31 + b.m24 * a.m32 + b.m34 * a.m33 + b.m44 * a.m34,
				b.m11 * a.m41 + b.m21 * a.m42 + b.m31 * a.m43 + b.m41 * a.m44,
				b.m12 * a.m41 + b.m22 * a.m42 + b.m32 * a.m43 + b.m42 * a.m44,
				b.m13 * a.m41 + b.m23 * a.m42 + b.m33 * a.m43 + b.m43 * a.m44,
				b.m14 * a.m41 + b.m24 * a.m42 + b.m34 * a.m43 + b.m44 * a.m44);
		}


		inline static Matrix4 MakeProjection(const float fFOV, const float fAspect, const float fNearPlane, const float fFarPlane)
		{
			const float s = 1.0f / tanf(fFOV * 0.5f);
			const float Q = fFarPlane / (fFarPlane - fNearPlane);

			return Matrix4(s / fAspect, 0.0f, 0.0f, 0.0f,
				0.0f, s, 0.0f, 0.0f,
				0.0f, 0.0f, Q, 1.0f,
				0.0f, 0.0f, -Q * fNearPlane, 0.0f);
		}

		inline static Matrix4 MakeView(const Vector3& vPos,
			const Vector3& vLookAt,
			const Vector3& vUp = Vector3(0.0f, 1.0f, 0.0f))
		{
			// Die z-Achse des Kamerakoordinatensystems berechnen
			Vector3 vZAxis((vLookAt - vPos).NormalizedCopy());

			// Die x-Achse ist das Kreuzprodukt aus y- und z-Achse
			Vector3 vXAxis(vUp.Cross(vZAxis).NormalizedCopy());

			// y-Achse berechnen
			Vector3 vYAxis(vZAxis.Cross(vXAxis).NormalizedCopy());

			// Rotationsmatrix erzeugen und die Translationsmatrix mit ihr multiplizieren
			return Matrix4::MakeTranslation(-vPos) *
				Matrix4(vXAxis.x, vYAxis.x, vZAxis.x, 0.0f,
					vXAxis.y, vYAxis.y, vZAxis.y, 0.0f,
					vXAxis.z, vYAxis.z, vZAxis.z, 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f);
		}

		void Transpose()
		{
			*this = Matrix4(m11, m21, m31, m41,
				m12, m22, m32, m42,
				m13, m23, m33, m43,
				m14, m24, m34, m44);
		}
	};


}
