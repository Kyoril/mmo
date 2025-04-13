// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "vector3.h"
#include "math_utils.h"

#include "base/macros.h"

#include <utility>
#include <cstring>

#include "matrix3.h"
#include "quaternion.h"
#include "vector4.h"

#include <immintrin.h> // AVX

namespace mmo
{
	class alignas(16) Matrix4
	{
	protected:
		/// The matrix entries, indexed by [row][col].
		union {
			float m[4][4];
			float _m[16];
		};

	public:
		/// Default constructor.
		Matrix4()
		{
			for (size_t i = 0; i < 16; ++i)
			{
				_m[i] = 0.0f;
			}
		}

		Matrix4(
			float m00, float m01, float m02, float m03,
			float m10, float m11, float m12, float m13,
			float m20, float m21, float m22, float m23,
			float m30, float m31, float m32, float m33)
		{
			m[0][0] = m00;
			m[0][1] = m01;
			m[0][2] = m02;
			m[0][3] = m03;
			m[1][0] = m10;
			m[1][1] = m11;
			m[1][2] = m12;
			m[1][3] = m13;
			m[2][0] = m20;
			m[2][1] = m21;
			m[2][2] = m22;
			m[2][3] = m23;
			m[3][0] = m30;
			m[3][1] = m31;
			m[3][2] = m32;
			m[3][3] = m33;
		}

		Matrix4(const float* arr)
		{
			std::memcpy(m, arr, 16 * sizeof(float));
		}

		/// Creates a standard 4x4 transformation matrix with a zero translation part from a rotation/scaling 3x3 matrix.
		inline Matrix4(const Matrix3& m3x3)
		{
			operator=(Identity);
			operator=(m3x3);
		}

		/// Creates a standard 4x4 transformation matrix with a zero translation part from a rotation/scaling Quaternion.
		inline Matrix4(const Quaternion& rot)
		{
			Matrix3 m3x3;
			rot.ToRotationMatrix(m3x3);
			operator=(Identity);
			operator=(m3x3);
		}

		Matrix3 Linear() const
		{
			return Matrix3(m[0][0], m[0][1], m[0][2],
				m[1][0], m[1][1], m[1][2],
				m[2][0], m[2][1], m[2][2]);
		}

		/// Exchange the contents of this matrix with another.
		void swap(Matrix4& other)
		{
			std::swap(m[0][0], other.m[0][0]);
			std::swap(m[0][1], other.m[0][1]);
			std::swap(m[0][2], other.m[0][2]);
			std::swap(m[0][3], other.m[0][3]);
			std::swap(m[1][0], other.m[1][0]);
			std::swap(m[1][1], other.m[1][1]);
			std::swap(m[1][2], other.m[1][2]);
			std::swap(m[1][3], other.m[1][3]);
			std::swap(m[2][0], other.m[2][0]);
			std::swap(m[2][1], other.m[2][1]);
			std::swap(m[2][2], other.m[2][2]);
			std::swap(m[2][3], other.m[2][3]);
			std::swap(m[3][0], other.m[3][0]);
			std::swap(m[3][1], other.m[3][1]);
			std::swap(m[3][2], other.m[3][2]);
			std::swap(m[3][3], other.m[3][3]);
		}

		void operator = (const Matrix3& mat3)
		{
			m[0][0] = mat3.m[0][0]; m[0][1] = mat3.m[0][1]; m[0][2] = mat3.m[0][2];
			m[1][0] = mat3.m[1][0]; m[1][1] = mat3.m[1][1]; m[1][2] = mat3.m[1][2];
			m[2][0] = mat3.m[2][0]; m[2][1] = mat3.m[2][1]; m[2][2] = mat3.m[2][2];
		}
		
		float* operator [] (size_t iRow)
		{
			ASSERT(iRow < 4);
			return m[iRow];
		}

		const float *operator [] (size_t iRow) const
		{
			ASSERT(iRow < 4);
			return m[iRow];
		}

#        define _mm_madd_ps( a, b, c ) _mm_add_ps( c, _mm_mul_ps( a, b ) )

		Matrix4 Concatenate(const Matrix4& B) const
		{
#if 1
			Matrix4 r;
			__m128 m2[4];
			m2[0] = _mm_load_ps(&B[0][0]);
			m2[1] = _mm_load_ps(&B[1][0]);
			m2[2] = _mm_load_ps(&B[2][0]);
			m2[3] = _mm_load_ps(&B[3][0]);

			__m128 t = _mm_mul_ps(_mm_load_ps1(&m[0][0]), m2[0]);
			t = _mm_madd_ps(_mm_load_ps1(&m[0][1]), m2[1], t);
			t = _mm_madd_ps(_mm_load_ps1(&m[0][2]), m2[2], t);
			t = _mm_madd_ps(_mm_load_ps1(&m[0][3]), m2[3], t);
			_mm_store_ps(r._m + 0, t);

			t = _mm_mul_ps(_mm_load_ps1(&m[1][0]), m2[0]);
			t = _mm_madd_ps(_mm_load_ps1(&m[1][1]), m2[1], t);
			t = _mm_madd_ps(_mm_load_ps1(&m[1][2]), m2[2], t);
			t = _mm_madd_ps(_mm_load_ps1(&m[1][3]), m2[3], t);
			_mm_store_ps(r._m + 4, t);

			t = _mm_mul_ps(_mm_load_ps1(&m[2][0]), m2[0]);
			t = _mm_madd_ps(_mm_load_ps1(&m[2][1]), m2[1], t);
			t = _mm_madd_ps(_mm_load_ps1(&m[2][2]), m2[2], t);
			t = _mm_madd_ps(_mm_load_ps1(&m[2][3]), m2[3], t);
			_mm_store_ps(r._m + 8, t);

			t = _mm_mul_ps(_mm_load_ps1(&m[3][0]), m2[0]);
			t = _mm_madd_ps(_mm_load_ps1(&m[3][1]), m2[1], t);
			t = _mm_madd_ps(_mm_load_ps1(&m[3][2]), m2[2], t);
			t = _mm_madd_ps(_mm_load_ps1(&m[3][3]), m2[3], t);
			_mm_store_ps(r._m + 12, t);

			return r;
#else
			Matrix4 result{};
			for (int row = 0; row < 4; ++row) {
				for (int col = 0; col < 4; ++col) {
					float sum = 0.0f;
					for (int k = 0; k < 4; ++k) {
						sum += m[row][k] * B.m[k][col];
					}
					result.m[row][col] = sum;
				}
			}
			return result;
#endif
		}

		Matrix4 operator * (const Matrix4 &m2) const
		{
			return Concatenate(m2);
		}

		Vector3 operator * (const Vector3& v) const
		{
			Vector3 r;

			float fInvW = 1.0f / (m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3]);

			r.x = (m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) * fInvW;
			r.y = (m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) * fInvW;
			r.z = (m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]) * fInvW;

			return r;
		}

		inline Vector4 operator*(const Vector4& v) const
		{
			return Vector4(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w,
				m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w,
				m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w,
				m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w);
		}

		Matrix4 operator + (const Matrix4 &m2) const
		{
			Matrix4 r;

			r.m[0][0] = m[0][0] + m2.m[0][0];
			r.m[0][1] = m[0][1] + m2.m[0][1];
			r.m[0][2] = m[0][2] + m2.m[0][2];
			r.m[0][3] = m[0][3] + m2.m[0][3];

			r.m[1][0] = m[1][0] + m2.m[1][0];
			r.m[1][1] = m[1][1] + m2.m[1][1];
			r.m[1][2] = m[1][2] + m2.m[1][2];
			r.m[1][3] = m[1][3] + m2.m[1][3];

			r.m[2][0] = m[2][0] + m2.m[2][0];
			r.m[2][1] = m[2][1] + m2.m[2][1];
			r.m[2][2] = m[2][2] + m2.m[2][2];
			r.m[2][3] = m[2][3] + m2.m[2][3];

			r.m[3][0] = m[3][0] + m2.m[3][0];
			r.m[3][1] = m[3][1] + m2.m[3][1];
			r.m[3][2] = m[3][2] + m2.m[3][2];
			r.m[3][3] = m[3][3] + m2.m[3][3];

			return r;
		}

		Matrix4 operator - (const Matrix4 &m2) const
		{
			Matrix4 r;
			r.m[0][0] = m[0][0] - m2.m[0][0];
			r.m[0][1] = m[0][1] - m2.m[0][1];
			r.m[0][2] = m[0][2] - m2.m[0][2];
			r.m[0][3] = m[0][3] - m2.m[0][3];

			r.m[1][0] = m[1][0] - m2.m[1][0];
			r.m[1][1] = m[1][1] - m2.m[1][1];
			r.m[1][2] = m[1][2] - m2.m[1][2];
			r.m[1][3] = m[1][3] - m2.m[1][3];

			r.m[2][0] = m[2][0] - m2.m[2][0];
			r.m[2][1] = m[2][1] - m2.m[2][1];
			r.m[2][2] = m[2][2] - m2.m[2][2];
			r.m[2][3] = m[2][3] - m2.m[2][3];

			r.m[3][0] = m[3][0] - m2.m[3][0];
			r.m[3][1] = m[3][1] - m2.m[3][1];
			r.m[3][2] = m[3][2] - m2.m[3][2];
			r.m[3][3] = m[3][3] - m2.m[3][3];

			return r;
		}

		bool operator == (const Matrix4& m2) const
		{
			if (
				m[0][0] != m2.m[0][0] || m[0][1] != m2.m[0][1] || m[0][2] != m2.m[0][2] || m[0][3] != m2.m[0][3] ||
				m[1][0] != m2.m[1][0] || m[1][1] != m2.m[1][1] || m[1][2] != m2.m[1][2] || m[1][3] != m2.m[1][3] ||
				m[2][0] != m2.m[2][0] || m[2][1] != m2.m[2][1] || m[2][2] != m2.m[2][2] || m[2][3] != m2.m[2][3] ||
				m[3][0] != m2.m[3][0] || m[3][1] != m2.m[3][1] || m[3][2] != m2.m[3][2] || m[3][3] != m2.m[3][3])
				return false;
			return true;
		}

		bool operator != (const Matrix4& m2) const
		{
			if (
				m[0][0] != m2.m[0][0] || m[0][1] != m2.m[0][1] || m[0][2] != m2.m[0][2] || m[0][3] != m2.m[0][3] ||
				m[1][0] != m2.m[1][0] || m[1][1] != m2.m[1][1] || m[1][2] != m2.m[1][2] || m[1][3] != m2.m[1][3] ||
				m[2][0] != m2.m[2][0] || m[2][1] != m2.m[2][1] || m[2][2] != m2.m[2][2] || m[2][3] != m2.m[2][3] ||
				m[3][0] != m2.m[3][0] || m[3][1] != m2.m[3][1] || m[3][2] != m2.m[3][2] || m[3][3] != m2.m[3][3])
				return true;
			return false;
		}
		
		Matrix4 Transpose() const
		{
			return Matrix4(m[0][0], m[1][0], m[2][0], m[3][0],
				m[0][1], m[1][1], m[2][1], m[3][1],
				m[0][2], m[1][2], m[2][2], m[3][2],
				m[0][3], m[1][3], m[2][3], m[3][3]);
		}

		void SetTrans(const Vector3& v)
		{
			m[0][3] = v.x;
			m[1][3] = v.y;
			m[2][3] = v.z;
		}

		Vector3 GetTrans() const
		{
			return Vector3(m[0][3], m[1][3], m[2][3]);
		}

		void MakeTrans(const Vector3& v)
		{
			m[0][0] = 1.0; m[0][1] = 0.0; m[0][2] = 0.0; m[0][3] = v.x;
			m[1][0] = 0.0; m[1][1] = 1.0; m[1][2] = 0.0; m[1][3] = v.y;
			m[2][0] = 0.0; m[2][1] = 0.0; m[2][2] = 1.0; m[2][3] = v.z;
			m[3][0] = 0.0; m[3][1] = 0.0; m[3][2] = 0.0; m[3][3] = 1.0;
		}

		void MakeTrans(float tx, float ty, float tz)
		{
			m[0][0] = 1.0; m[0][1] = 0.0; m[0][2] = 0.0; m[0][3] = tx;
			m[1][0] = 0.0; m[1][1] = 1.0; m[1][2] = 0.0; m[1][3] = ty;
			m[2][0] = 0.0; m[2][1] = 0.0; m[2][2] = 1.0; m[2][3] = tz;
			m[3][0] = 0.0; m[3][1] = 0.0; m[3][2] = 0.0; m[3][3] = 1.0;
		}

		bool IsNearlyEqual(const Matrix4& matrix4) const
		{
			for (size_t i = 0; i < 16; ++i)
			{
				if (::fabsf(_m[i] - matrix4._m[i]) > 1e-5f)
				{
					return false;
				}
			}
			return true;
		}

		static Matrix4 GetTrans(const Vector3& v)
		{
			Matrix4 r;

			r.m[0][0] = 1.0; r.m[0][1] = 0.0; r.m[0][2] = 0.0; r.m[0][3] = v.x;
			r.m[1][0] = 0.0; r.m[1][1] = 1.0; r.m[1][2] = 0.0; r.m[1][3] = v.y;
			r.m[2][0] = 0.0; r.m[2][1] = 0.0; r.m[2][2] = 1.0; r.m[2][3] = v.z;
			r.m[3][0] = 0.0; r.m[3][1] = 0.0; r.m[3][2] = 0.0; r.m[3][3] = 1.0;

			return r;
		}

		static Matrix4 GetTrans(float t_x, float t_y, float t_z)
		{
			Matrix4 r;

			r.m[0][0] = 1.0; r.m[0][1] = 0.0; r.m[0][2] = 0.0; r.m[0][3] = t_x;
			r.m[1][0] = 0.0; r.m[1][1] = 1.0; r.m[1][2] = 0.0; r.m[1][3] = t_y;
			r.m[2][0] = 0.0; r.m[2][1] = 0.0; r.m[2][2] = 1.0; r.m[2][3] = t_z;
			r.m[3][0] = 0.0; r.m[3][1] = 0.0; r.m[3][2] = 0.0; r.m[3][3] = 1.0;

			return r;
		}

		void SetScale(const Vector3& v)
		{
			m[0][0] = v.x;
			m[1][1] = v.y;
			m[2][2] = v.z;
		}

		Vector3 GetScale() const
		{
			return Vector3(m[0][0], m[1][1], m[2][2]);
		}

		static Matrix4 GetScale(const Vector3& v)
		{
			Matrix4 r;
			r.m[0][0] = v.x; r.m[0][1] = 0.0; r.m[0][2] = 0.0; r.m[0][3] = 0.0;
			r.m[1][0] = 0.0; r.m[1][1] = v.y; r.m[1][2] = 0.0; r.m[1][3] = 0.0;
			r.m[2][0] = 0.0; r.m[2][1] = 0.0; r.m[2][2] = v.z; r.m[2][3] = 0.0;
			r.m[3][0] = 0.0; r.m[3][1] = 0.0; r.m[3][2] = 0.0; r.m[3][3] = 1.0;

			return r;
		}

		static Matrix4 GetScale(float s_x, float s_y, float s_z)
		{
			Matrix4 r;
			r.m[0][0] = s_x; r.m[0][1] = 0.0; r.m[0][2] = 0.0; r.m[0][3] = 0.0;
			r.m[1][0] = 0.0; r.m[1][1] = s_y; r.m[1][2] = 0.0; r.m[1][3] = 0.0;
			r.m[2][0] = 0.0; r.m[2][1] = 0.0; r.m[2][2] = s_z; r.m[2][3] = 0.0;
			r.m[3][0] = 0.0; r.m[3][1] = 0.0; r.m[3][2] = 0.0; r.m[3][3] = 1.0;

			return r;
		}

		inline void Extract3x3Matrix(Matrix3& m3x3) const
		{
			m3x3.m[0][0] = m[0][0];
			m3x3.m[0][1] = m[0][1];
			m3x3.m[0][2] = m[0][2];
			m3x3.m[1][0] = m[1][0];
			m3x3.m[1][1] = m[1][1];
			m3x3.m[1][2] = m[1][2];
			m3x3.m[2][0] = m[2][0];
			m3x3.m[2][1] = m[2][1];
			m3x3.m[2][2] = m[2][2];
		}

		bool HasScale() const
		{
			// check magnitude of column vectors (==local axes)
			float t = m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0];
			if (!FloatEqual(t, 1.0, (float)1e-04))
				return true;
			t = m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1];
			if (!FloatEqual(t, 1.0, (float)1e-04))
				return true;
			t = m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2];
			if (!FloatEqual(t, 1.0, (float)1e-04))
				return true;

			return false;
		}

		bool HasNegativeScale() const
		{
			return Determinant() < 0;
		}

		inline Quaternion ExtractQuaternion() const
		{
			Matrix3 m3x3;
			Extract3x3Matrix(m3x3);
			return Quaternion(m3x3);
		}

		static const Matrix4 Zero;
		static const Matrix4 ZeroAffine;
		static const Matrix4 Identity;

		Matrix4 operator*(float scalar) const
		{
			return Matrix4(
				scalar*m[0][0], scalar*m[0][1], scalar*m[0][2], scalar*m[0][3],
				scalar*m[1][0], scalar*m[1][1], scalar*m[1][2], scalar*m[1][3],
				scalar*m[2][0], scalar*m[2][1], scalar*m[2][2], scalar*m[2][3],
				scalar*m[3][0], scalar*m[3][1], scalar*m[3][2], scalar*m[3][3]);
		}

		friend std::ostream &operator<<(std::ostream &o, const Matrix4 &mat);

		Matrix4 Adjoint() const;
		float Determinant() const;
		Matrix4 Inverse() const;

		void MakeTransform(const Vector3& position, const Vector3& scale, const Quaternion& orientation);
		void MakeInverseTransform(const Vector3& position, const Vector3& scale, const Quaternion& orientation);
		void Decomposition(Vector3& position, Vector3& scale, Quaternion& orientation) const;

		bool IsAffine() const
		{
			return m[3][0] == 0 && m[3][1] == 0 && m[3][2] == 0 && m[3][3] == 1;
		}

		Matrix4 InverseAffine() const;
		Matrix4 ConcatenateAffine(const Matrix4 &m2) const
		{
			ASSERT(IsAffine() && m2.IsAffine());

			return Matrix4(
				m[0][0] * m2.m[0][0] + m[0][1] * m2.m[1][0] + m[0][2] * m2.m[2][0],
				m[0][0] * m2.m[0][1] + m[0][1] * m2.m[1][1] + m[0][2] * m2.m[2][1],
				m[0][0] * m2.m[0][2] + m[0][1] * m2.m[1][2] + m[0][2] * m2.m[2][2],
				m[0][0] * m2.m[0][3] + m[0][1] * m2.m[1][3] + m[0][2] * m2.m[2][3] + m[0][3],

				m[1][0] * m2.m[0][0] + m[1][1] * m2.m[1][0] + m[1][2] * m2.m[2][0],
				m[1][0] * m2.m[0][1] + m[1][1] * m2.m[1][1] + m[1][2] * m2.m[2][1],
				m[1][0] * m2.m[0][2] + m[1][1] * m2.m[1][2] + m[1][2] * m2.m[2][2],
				m[1][0] * m2.m[0][3] + m[1][1] * m2.m[1][3] + m[1][2] * m2.m[2][3] + m[1][3],

				m[2][0] * m2.m[0][0] + m[2][1] * m2.m[1][0] + m[2][2] * m2.m[2][0],
				m[2][0] * m2.m[0][1] + m[2][1] * m2.m[1][1] + m[2][2] * m2.m[2][1],
				m[2][0] * m2.m[0][2] + m[2][1] * m2.m[1][2] + m[2][2] * m2.m[2][2],
				m[2][0] * m2.m[0][3] + m[2][1] * m2.m[1][3] + m[2][2] * m2.m[2][3] + m[2][3],

				0, 0, 0, 1);
		}

		Vector3 TransformDirectionAffine(const Vector3& v) const
		{
			ASSERT(IsAffine());

			return Vector3(
				m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
				m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
				m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
		}

		Vector3 TransformAffine(const Vector3& v) const
		{
			ASSERT(IsAffine());

			return Vector3(
				m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3],
				m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
				m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]);
		}
	};

	inline std::ostream& operator<<(std::ostream& o, const mmo::Matrix4& b)
	{
		return o
			<< "(" << b.m[0][0] << ", " << b.m[0][1] << ", " << b.m[0][2] << ", " << b.m[0][3] << ",\n"
			<< "(" << b.m[1][0] << ", " << b.m[1][1] << ", " << b.m[1][2] << ", " << b.m[1][3] << ",\n"
			<< "(" << b.m[2][0] << ", " << b.m[2][1] << ", " << b.m[2][2] << ", " << b.m[2][3] << ",\n"
			<< "(" << b.m[3][0] << ", " << b.m[3][1] << ", " << b.m[3][2] << ", " << b.m[3][3] << ")";
	}
}
