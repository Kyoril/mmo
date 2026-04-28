// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "math_utils.h"
#include "matrix4.h"
#include "matrix3.h"
#include "quaternion.h"
#include "vector3.h"

#include <cmath>
#include <algorithm>


namespace mmo
{
	bool FloatEqual(float a, float b, float tolerance)
	{
		return std::fabs(b - a) <= tolerance;
	}

	Matrix4 MakeViewMatrix(const Vector3& position, const Quaternion& orientation)
	{
		// View matrix is:
		//
		//  [ Lx  Uy  Dz  Tx  ]
		//  [ Lx  Uy  Dz  Ty  ]
		//  [ Lx  Uy  Dz  Tz  ]
		//  [ 0   0   0   1   ]
		//
		// Where T = -(Transposed(Rot) * Pos)

		// This is most efficiently done using 3x3 Matrices
		Matrix3 rot{};
		orientation.ToRotationMatrix(rot);

		// Make the translation relative to new axes
		const auto rotT = rot.Transpose();
		const auto trans = -rotT * position;

		// Make final matrix
		auto viewMatrix = Matrix4::Identity;
		viewMatrix = rotT;
		viewMatrix[0][3] = trans.x;
		viewMatrix[1][3] = trans.y;
		viewMatrix[2][3] = trans.z;

		return viewMatrix;
	}

	EncodedNormal8 EncodeNormalSNorm8(float nx, float ny, float nz)
	{
		EncodedNormal8 out;

		// clamp input to [-1, +1]
		nx = std::fmax(-1.0f, std::fmin(1.0f, nx));
		ny = std::fmax(-1.0f, std::fmin(1.0f, ny));
		nz = std::fmax(-1.0f, std::fmin(1.0f, nz));

		// map to [-128..127], rounding to nearest
		// note that -1 maps to -128, +1 maps to +127
		const float fx = std::round(nx * 127.0f);
		const float fy = std::round(ny * 127.0f);
		const float fz = std::round(nz * 127.0f);

		// convert to int
		int ix = static_cast<int>(fx);
		int iy = static_cast<int>(fy);
		int iz = static_cast<int>(fz);

		// clamp to [-128..127]
		ix = std::max(-128, std::min(127, ix));
		iy = std::max(-128, std::min(127, iy));
		iz = std::max(-128, std::min(127, iz));

		out.x = static_cast<int8_t>(ix);
		out.y = static_cast<int8_t>(iy);
		out.z = static_cast<int8_t>(iz);
		return out;
	}

	void DecodeNormalSNorm8(const EncodedNormal8& enc, float& nx, float& ny, float& nz)
	{
		// convert from [-128..127] into [-1..+1], except that -128 => -1, +127 => ~+0.992
		// If you want symmetrical extremes, you might do:
		// nx = (enc.x == -128) ? -1.0f : (float)enc.x / 127.0f;
		// But let's keep it simpler:
		nx = static_cast<float>(enc.x) / 127.0f;
		ny = static_cast<float>(enc.y) / 127.0f;
		nz = static_cast<float>(enc.z) / 127.0f;
	}

	float EaseInOutQuad(float t)
	{
		// Clamp t to [0, 1]
		if (t <= 0.0f) return 0.0f;
		if (t >= 1.0f) return 1.0f;

		// Acceleration until halfway, then deceleration
		if (t < 0.5f)
		{
			return 2.0f * t * t;
		}
		else
		{
			const float mt = 1.0f - t;
			return 1.0f - 2.0f * mt * mt;
		}
	}

	float EaseInOutCubic(float t)
	{
		// Clamp t to [0, 1]
		if (t <= 0.0f) return 0.0f;
		if (t >= 1.0f) return 1.0f;

		// Smoother acceleration/deceleration than quadratic
		if (t < 0.5f)
		{
			return 4.0f * t * t * t;
		}
		else
		{
			const float mt = 1.0f - t;
			return 1.0f - 4.0f * mt * mt * mt;
		}
	}

	float ComputeSegmentAngle(const Vector3& prev, const Vector3& curr, const Vector3& next)
	{
		// Compute vectors from curr to prev and from curr to next
		const Vector3 toPrev = (prev - curr).NormalizedCopy();
		const Vector3 toNext = (next - curr).NormalizedCopy();

		// Compute the dot product to get the cosine of the angle
		const float dotProduct = toPrev.Dot(toNext);

		// Clamp to [-1, 1] to handle floating-point errors
		const float clamped = std::fmax(-1.0f, std::fmin(1.0f, dotProduct));

		// Compute angle in radians
		const float angle = std::acos(clamped);

		// Return angle in [0, Pi]
		return angle;
	}
}
