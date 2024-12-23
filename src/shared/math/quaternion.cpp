// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "quaternion.h"
#include "matrix3.h"
#include "radian.h"


namespace mmo
{
	const float Quaternion::Epsilon = 1e-03f;
	const Quaternion Quaternion::Zero(0.0f, 0.0f, 0.0f, 0.0f);
	const Quaternion Quaternion::Identity(1.0f, 0.0f, 0.0f, 0.0f);
	
	void Quaternion::FromRotationMatrix(const Matrix3& kRot)
	{
		const auto fTrace = kRot[0][0] + kRot[1][1] + kRot[2][2];
		
		float fRoot;

		if (fTrace > 0.0f)
		{
			fRoot = ::sqrtf(fTrace + 1.0f);
			w = 0.5f * fRoot;
			fRoot = 0.5f / fRoot;
			x = (kRot[2][1] - kRot[1][2]) * fRoot;
			y = (kRot[0][2] - kRot[2][0]) * fRoot;
			z = (kRot[1][0] - kRot[0][1]) * fRoot;
		}
		else
		{
			static size_t s_iNext[3] = { 1, 2, 0 };
			size_t i = 0;
			if (kRot[1][1] > kRot[0][0])
				i = 1;
			if (kRot[2][2] > kRot[i][i])
				i = 2;
			const auto j = s_iNext[i];
			const auto k = s_iNext[j];

			fRoot = ::sqrtf(kRot[i][i] - kRot[j][j] - kRot[k][k] + 1.0f);
			float* quaternion[3] = { &x, &y, &z };
			*quaternion[i] = 0.5f * fRoot;
			fRoot = 0.5f / fRoot;
			w = (kRot[k][j] - kRot[j][k]) * fRoot;
			*quaternion[j] = (kRot[j][i] + kRot[i][j]) * fRoot;
			*quaternion[k] = (kRot[k][i] + kRot[i][k]) * fRoot;
		}
	}

	void Quaternion::ToRotationMatrix(Matrix3& kRot) const
	{
		const auto fTx = x + x;
		const auto fTy = y + y;
		const auto fTz = z + z;
		const auto fTwx = fTx * w;
		const auto fTwy = fTy * w;
		const auto fTwz = fTz * w;
		const auto fTxx = fTx * x;
		const auto fTxy = fTy * x;
		const auto fTxz = fTz * x;
		const auto fTyy = fTy * y;
		const auto fTyz = fTz * y;
		const auto fTzz = fTz * z;

		kRot[0][0] = 1.0f - (fTyy + fTzz);
		kRot[0][1] = fTxy - fTwz;
		kRot[0][2] = fTxz + fTwy;
		kRot[1][0] = fTxy + fTwz;
		kRot[1][1] = 1.0f - (fTxx + fTzz);
		kRot[1][2] = fTyz - fTwx;
		kRot[2][0] = fTxz - fTwy;
		kRot[2][1] = fTyz + fTwx;
		kRot[2][2] = 1.0f - (fTxx + fTyy);
	}

	void Quaternion::FromAngleAxis(const Radian& angle, const Vector3 & axis)
	{
		const auto halfAngle(0.5f * angle);
		
		const auto sin = ::sinf(halfAngle.GetValueRadians());
		w = ::cosf(halfAngle.GetValueRadians());
		x = sin * axis.x;
		y = sin * axis.y;
		z = sin * axis.z;
	}
	
	void Quaternion::ToAngleAxis(Radian& angle, Vector3& axis) const
	{
		const auto squaredLength = x * x + y * y + z * z;
		
		if (squaredLength > 0.0f)
		{
			angle = 2.0f * ::acosf(w);
			
			const auto invLength = 1.0f / ::sqrtf(squaredLength);
			axis.x = x * invLength;
			axis.y = y * invLength;
			axis.z = z * invLength;
		}
		else
		{
			angle = Radian(0.0f);
			axis.x = 1.0;
			axis.y = 0.0;
			axis.z = 0.0;
		}
	}
	
	void Quaternion::FromAxes(const Vector3 * axis)
	{
		Matrix3 rot{};

		for (size_t iCol = 0; iCol < 3; iCol++)
		{
			rot[0][iCol] = axis[iCol].x;
			rot[1][iCol] = axis[iCol].y;
			rot[2][iCol] = axis[iCol].z;
		}

		FromRotationMatrix(rot);
	}
	
	void Quaternion::FromAxes(const Vector3 & xAxis, const Vector3 & yAxis, const Vector3 & zAxis)
	{
		Matrix3 rot{};

		rot[0][0] = xAxis.x;
		rot[1][0] = xAxis.y;
		rot[2][0] = xAxis.z;

		rot[0][1] = yAxis.x;
		rot[1][1] = yAxis.y;
		rot[2][1] = yAxis.z;

		rot[0][2] = zAxis.x;
		rot[1][2] = zAxis.y;
		rot[2][2] = zAxis.z;

		FromRotationMatrix(rot);
	}
	
	void Quaternion::ToAxes(Vector3 * axis) const
	{
		Matrix3 rot{};

		ToRotationMatrix(rot);

		for (auto cols = 0; cols < 3; cols++)
		{
			axis[cols].x = rot[0][cols];
			axis[cols].y = rot[1][cols];
			axis[cols].z = rot[2][cols];
		}
	}
	
	void Quaternion::ToAxes(Vector3 & xAxis, Vector3 & yAxis, Vector3 & zAxis) const
	{
		Matrix3 rot{};

		ToRotationMatrix(rot);

		xAxis.x = rot[0][0];
		xAxis.y = rot[1][0];
		xAxis.z = rot[2][0];

		yAxis.x = rot[0][1];
		yAxis.y = rot[1][1];
		yAxis.z = rot[2][1];

		zAxis.x = rot[0][2];
		zAxis.y = rot[1][2];
		zAxis.z = rot[2][2];
	}
	
	Vector3 Quaternion::GetXAxis() const
	{
		const auto ty = 2.0f * y;
		const auto tz = 2.0f * z;
		const auto twy = ty * w;
		const auto twz = tz * w;
		const auto txy = ty * x;
		const auto txz = tz * x;
		const auto tyy = ty * y;
		const auto tzz = tz * z;

		return Vector3(1.0f - (tyy + tzz), txy + twz, txz - twy);
	}
	
	Vector3 Quaternion::GetYAxis() const
	{
		const auto tx = 2.0f * x;
		const auto ty = 2.0f * y;
		const auto tz = 2.0f * z;
		const auto twx = tx * w;
		const auto twz = tz * w;
		const auto txx = tx * x;
		const auto txy = ty * x;
		const auto tyz = tz * y;
		const auto tzz = tz * z;

		return Vector3(txy - twz, 1.0f - (txx + tzz), tyz + twx);
	}
	
	Vector3 Quaternion::GetZAxis() const
	{
		const auto tx = 2.0f * x;
		const auto ty = 2.0f * y;
		const auto tz = 2.0f * z;
		const auto twx = tx * w;
		const auto twy = ty * w;
		const auto txx = tx * x;
		const auto txz = tz * x;
		const auto tyy = ty * y;
		const auto tyz = tz * y;

		return Vector3(txz + twy, tyz - twx, 1.0f - (txx + tyy));
	}
	
	Quaternion Quaternion::operator+(const Quaternion & q) const
	{
		return Quaternion(w + q.w, x + q.x, y + q.y, z + q.z);
	}
	
	Quaternion Quaternion::operator-(const Quaternion & q) const
	{
		return Quaternion(w - q.w, x - q.x, y - q.y, z - q.z);
	}
	
	Quaternion Quaternion::operator*(const Quaternion & q) const
	{
		return Quaternion
		(
			w * q.w - x * q.x - y * q.y - z * q.z,
			w * q.x + x * q.w + y * q.z - z * q.y,
			w * q.y + y * q.w + z * q.x - x * q.z,
			w * q.z + z * q.w + x * q.y - y * q.x
		);
	}
	
	Quaternion Quaternion::operator*(float s) const
	{
		return Quaternion(s * w, s * x, s * y, s * z);
	}
	
	Quaternion Quaternion::operator-() const
	{
		return Quaternion(-w, -x, -y, -z);
	}
	
	float Quaternion::Dot(const Quaternion & q) const
	{
		return w * q.w + x * q.x + y * q.y + z * q.z;
	}
	
	float Quaternion::Norm() const
	{
		return w * w + x * x + y * y + z * z;
	}
	
	float Quaternion::Normalize()
	{
		const auto len = Norm();
		const auto factor = 1.0f / ::sqrtf(len);
		
		*this = *this * factor;
		
		return len;
	}
	
	Quaternion Quaternion::Inverse() const
	{
		const auto norm = w * w + x * x + y * y + z * z;
		if (norm > 0.0f)
		{
			const float invNorm = 1.0f / norm;
			return Quaternion(w * invNorm, -x * invNorm, -y * invNorm, -z * invNorm);
		}

		// return an invalid result to flag the error
		return Zero;
	}
	
	Quaternion Quaternion::UnitInverse() const
	{
		return Quaternion(w, -x, -y, -z);
	}
	
	Quaternion Quaternion::Exp() const
	{
		const Radian angle(::sqrtf(x * x + y * y + z * z));
		const auto sin = ::sinf(angle.GetValueRadians());

		Quaternion result;
		result.w = ::cosf(angle.GetValueRadians());

		if (::fabsf(sin) >= Epsilon)
		{
			const auto coefficient = sin / (angle.GetValueRadians());
			result.x = coefficient * x;
			result.y = coefficient * y;
			result.z = coefficient * z;
		}
		else
		{
			result.x = x;
			result.y = y;
			result.z = z;
		}

		return result;
	}
	
	Quaternion Quaternion::Log() const
	{
		Quaternion result;
		result.w = 0.0;

		if (::fabsf(w) < 1.0f)
		{
			const Radian angle(::acosf(w));
			
			const auto sin = ::sinf(angle.GetValueRadians());
			if (::fabsf(sin) >= Epsilon)
			{
				const auto coefficient = angle.GetValueRadians() / sin;
				result.x = coefficient * x;
				result.y = coefficient * y;
				result.z = coefficient * z;
				return result;
			}
		}

		result.x = x;
		result.y = y;
		result.z = z;

		return result;
	}
	
	Vector3 Quaternion::operator*(const Vector3 & v) const
	{
		const Vector3 vecQ(x, y, z);
		
		auto uv = vecQ.Cross(v);
		auto uuv = vecQ.Cross(uv);
		uv *= (2.0f * w);
		uuv *= 2.0f;

		return v + uv + uuv;
	}
	
	Radian Quaternion::GetRoll(bool reprojectAxis) const
	{
		if (reprojectAxis)
		{
			const auto fTy = 2.0f * y;
			const auto fTz = 2.0f * z;
			const auto fTwz = fTz * w;
			const auto fTxy = fTy * x;
			const auto fTyy = fTy * y;
			const auto fTzz = fTz * z;

			return Radian(::atan2f(fTxy + fTwz, 1.0f - (fTyy + fTzz)));

		}

		return Radian(::atan2f(2 * (x * y + w * z), w * w + x * x - y * y - z * z));
	}
	
	Radian Quaternion::GetPitch(bool reprojectAxis) const
	{
		if (reprojectAxis)
		{
			const auto fTx = 2.0f * x;
			const auto fTz = 2.0f * z;
			const auto fTwx = fTx * w;
			const auto fTxx = fTx * x;
			const auto fTyz = fTz * y;
			const auto fTzz = fTz * z;

			return Radian(::atan2f(fTyz + fTwx, 1.0f - (fTxx + fTzz)));
		}

		return Radian(::atan2f(2 * (y * z + w * x), w * w - x * x - y * y + z * z));
	}
	
	Radian Quaternion::GetYaw(bool reprojectAxis) const
	{
		if (reprojectAxis)
		{
			const auto fTx = 2.0f * x;
			const auto fTy = 2.0f * y;
			const auto fTz = 2.0f * z;
			const auto fTwy = fTy * w;
			const auto fTxx = fTx * x;
			const auto fTxz = fTz * x;
			const auto fTyy = fTy * y;

			return Radian(::atan2f(fTxz + fTwy, 1.0f - (fTxx + fTyy)));
		}

		return Radian(::asinf(-2 * (x * z - w * y)));
	}
	
	bool Quaternion::Equals(const Quaternion & rhs, const Radian& tolerance) const
	{
		const auto cos = Dot(rhs);
		const auto angle = Radian(::acosf(cos));

		return ::fabsf(angle.GetValueRadians()) <= tolerance.GetValueRadians()
			|| FloatEqual(angle.GetValueRadians(), Pi, tolerance.GetValueRadians());
	}
	
	Quaternion Quaternion::Slerp(float t, const Quaternion & p, const Quaternion & q, bool shortestPath)
	{
		auto fCos = p.Dot(q);
		Quaternion qt;

		// Do we need to invert rotation?
		if (fCos < 0.0f && shortestPath)
		{
			fCos = -fCos;
			qt = -q;
		}
		else
		{
			qt = q;
		}

		if (::fabsf(fCos) < 1.0f - Epsilon)
		{
			const auto fSin = ::sqrtf(1 - ::sqrtf(fCos));
			const auto fAngle = Radian(::atan2f(fSin, fCos));
			const auto fInvSin = 1.0f / fSin;
			const auto coefficient0 = ::sinf(((1.0f - t) * fAngle).GetValueRadians()) * fInvSin;
			const auto coefficient1 = ::sinf((t * fAngle).GetValueRadians()) * fInvSin;
			return coefficient0 * p + coefficient1 * qt;
		}

		Quaternion result = (1.0f - t) * p + t * qt;
		result.Normalize();
		return result;
	}
	
	Quaternion Quaternion::SlerpExtraSpins(float t, const Quaternion & p, const Quaternion & q, int extraSpins)
	{
		const auto cos = p.Dot(q);
		const Radian angle(::acosf(cos));

		if (::fabsf(angle.GetValueRadians()) < Epsilon)
			return p;

		const auto sin = ::sinf(angle.GetValueRadians());
		const Radian phase(Pi * static_cast<float>(extraSpins) * t);
		const auto invSin = 1.0f / sin;
		const auto coefficient0 = ::sinf(((1.0f - t) * angle - phase).GetValueRadians()) * invSin;
		const auto coefficient1 = ::sinf((t * angle + phase).GetValueRadians()) * invSin;
		return coefficient0 * p + coefficient1 * q;
	}
	
	void Quaternion::Intermediate(const Quaternion & q0, const Quaternion & q1, const Quaternion & q2, Quaternion & a, Quaternion & b)
	{
		const auto q0Inv = q0.UnitInverse();
		const auto q1Inv = q0.UnitInverse();
		const auto p0 = q0Inv * q1;
		const auto p1 = q1Inv * q1;
		const auto arg = 0.25 * (p0.Log() - p1.Log());
		const auto minusArg = -arg;

		a = q1 * arg.Exp();
		b = q1 * minusArg.Exp();
	}
	
	Quaternion Quaternion::Squad(float t, const Quaternion & p, const Quaternion & a, const Quaternion & b, const Quaternion & q, bool shortestPath)
	{
		const auto slerpT = 2.0f * t * (1.0f - t);
		const auto slerpP = Slerp(t, p, q, shortestPath);
		const auto slerpQ = Slerp(t, a, b);
		return Slerp(slerpT, slerpP, slerpQ);
	}
	
	Quaternion Quaternion::NLerp(float t, const Quaternion & p, const Quaternion & q, bool shortestPath)
	{
		Quaternion result;
		
		const auto cos = p.Dot(q);
		if (cos < 0.0f && shortestPath)
		{
			result = p + t * (-q - p);
		}
		else
		{
			result = p + t * (q - p);
		}
		
		result.Normalize();
		return result;
	}
}
