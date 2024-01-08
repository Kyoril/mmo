// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <cstring>
#include <algorithm>

#include "degree.h"
#include "radian.h"
#include "math_utils.h"

namespace mmo
{
	class Vector3;

	class Matrix3 final
	{
		friend class Matrix4;
	private:
		float m[3][3];
		
	public:
		Matrix3() = default;
		explicit Matrix3(const float arr[3][3])
		{
			std::memcpy(m, arr, 9 * sizeof(float));
		}
		Matrix3(const Matrix3& r)
		{
			std::memcpy(m, r.m, 9 * sizeof(float));
		}
		Matrix3(float m00, float m01, float m02,
			float m10, float m11, float m12,
			float m20, float m21, float m22)
		{
			m[0][0] = m00;
			m[0][1] = m01;
			m[0][2] = m02;
			
			m[1][0] = m10;
			m[1][1] = m11;
			m[1][2] = m12;
			
			m[2][0] = m20;
			m[2][1] = m21;
			m[2][2] = m22;
		}
		Matrix3& operator= (const Matrix3& r)
		{
			memcpy(m, r.m, 9 * sizeof(float));
			return *this;
		}

	public:
		void swap(Matrix3& other) noexcept
		{
			std::swap(m[0][0], other.m[0][0]);
			std::swap(m[0][1], other.m[0][1]);
			std::swap(m[0][2], other.m[0][2]);
			std::swap(m[1][0], other.m[1][0]);
			std::swap(m[1][1], other.m[1][1]);
			std::swap(m[1][2], other.m[1][2]);
			std::swap(m[2][0], other.m[2][0]);
			std::swap(m[2][1], other.m[2][1]);
			std::swap(m[2][2], other.m[2][2]);
		}

	public:
		const float* operator[] (size_t row) const { return m[row]; }
		float* operator[] (size_t row) { return m[row]; }

	public:
		bool operator==(const Matrix3& r) const;
		bool operator!=(const Matrix3& r) const { return !operator==(r); }
		Matrix3 operator+(const Matrix3& r) const;
		Matrix3 operator-(const Matrix3& r) const;
		Matrix3 operator*(const Matrix3& r) const;
		Matrix3 operator-() const;
		Vector3 operator*(const Vector3& r) const;
		friend Vector3 operator* (const Vector3& vec, const Matrix3& mat);
		Matrix3 operator* (float scalar) const;
		friend Matrix3 operator* (float scalar, const Matrix3& mat);
		
	public:
		[[nodiscard]] Vector3 GetColumn(size_t col) const;

		void SetColumn(size_t col, const Vector3& vec);

		void FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis);

		[[nodiscard]] Matrix3 Transpose() const;

		bool Inverse(Matrix3& inverse, float tolerance = 1e-06f) const;

		[[nodiscard]] Matrix3 Inverse(float fTolerance = 1e-06f) const;

		[[nodiscard]] float Determinant() const;

		void SingularValueDecomposition(Matrix3& l, Vector3& s, Matrix3& r) const;

		void SingularValueComposition(const Matrix3& l, const Vector3& s, const Matrix3& r);

		void Orthonormalize();

		void QDUDecomposition(Matrix3& q, Vector3& d, Vector3& u) const;

		[[nodiscard]] float SpectralNorm() const;

		void ToAngleAxis(Vector3& axis, Radian& rfAngle) const;

		void ToAngleAxis(Vector3& rkAxis, Degree& rfAngle) const
		{
			Radian r;
			ToAngleAxis(rkAxis, r);
			rfAngle = r;
		}

		void FromAngleAxis(const Vector3& axis, const Radian& angle);

		bool ToEulerAnglesXYZ(Radian& yAngle, Radian& pAngle, Radian& rAngle) const;

		bool ToEulerAnglesXZY(Radian& yAngle, Radian& pAngle, Radian& rAngle) const;

		bool ToEulerAnglesYXZ(Radian& yAngle, Radian& pAngle, Radian& rAngle) const;

		bool ToEulerAnglesYZX(Radian& yAngle, Radian& pAngle, Radian& rAngle) const;

		bool ToEulerAnglesZXY(Radian& yAngle, Radian& pAngle, Radian& rAngle) const;

		bool ToEulerAnglesZYX(Radian& yAngle, Radian& pAngle, Radian& rAngle) const;

		void FromEulerAnglesXYZ(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle);

		void FromEulerAnglesXZY(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle);

		void FromEulerAnglesYXZ(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle);

		void FromEulerAnglesYZX(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle);

		void FromEulerAnglesZXY(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle);

		void FromEulerAnglesZYX(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle);

		void EigenSolveSymmetric(float eigenValue[3], Vector3 eigenVector[3]) const;
		
		static void TensorProduct(const Vector3& u, const Vector3& v, Matrix3& product);

		[[nodiscard]] bool HasScale() const
		{
			// check magnitude of column vectors (==local axes)
			float t = m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0];
			if (!FloatEqual(t, 1.0))
				return true;
			t = m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1];
			if (!FloatEqual(t, 1.0))
				return true;
			t = m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2];
			if (!FloatEqual(t, 1.0))
				return true;

			return false;
		}

	private:
		void Tridiagonal(float diagonals[3], float subDiagonals[3]);

		bool QLAlgorithm(float diagonals[3], float subDiagonals[3]);

	private:
		static const float msSvdEpsilon;
		static const unsigned int msSvdMaxIterations;
		
		static void BiDiagonalize(Matrix3& kA, Matrix3& kL, Matrix3& kR);

		static void GolubKahanStep(Matrix3& kA, Matrix3& kL, Matrix3& kR);

		static float MaxCubicRoot(float coefficients[3]);

	public:
		static const float Epsilon;
		static const Matrix3 Zero;
		static const Matrix3 Identity;
	};
}
