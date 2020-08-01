
#include "quaternion.h"


namespace mmo
{
	void Quaternion::FromAngleAxis(float angle, const Vector3 & axis)
	{
	}
	void Quaternion::ToAngleAxis(float & angle, Vector3 & axis) const
	{
	}
	void Quaternion::FromAxes(const Vector3 * axis)
	{
	}
	void Quaternion::FromAxes(const Vector3 & xAxis, const Vector3 & yAxis, const Vector3 & zAxis)
	{
	}
	void Quaternion::ToAxes(Vector3 * axis) const
	{
	}
	void Quaternion::ToAxes(Vector3 & xAxis, Vector3 & yAxis, Vector3 & zAxis) const
	{
	}
	Vector3 Quaternion::GetXAxis() const
	{
		return Vector3();
	}
	Vector3 Quaternion::GetYAxis() const
	{
		return Vector3();
	}
	Vector3 Quaternion::GetZAxis() const
	{
		return Vector3();
	}
	Quaternion Quaternion::operator+(const Quaternion & q) const
	{
		return Quaternion();
	}
	Quaternion Quaternion::operator-(const Quaternion & q) const
	{
		return Quaternion();
	}
	Quaternion Quaternion::operator*(const Quaternion & q) const
	{
		return Quaternion();
	}
	Quaternion Quaternion::operator*(float s) const
	{
		return Quaternion();
	}
	Quaternion Quaternion::operator-() const
	{
		return Quaternion();
	}
	float Quaternion::Dot(const Quaternion & q) const
	{
		return 0.0f;
	}
	float Quaternion::Norm() const
	{
		return 0.0f;
	}
	float Quaternion::Normalize()
	{
		return 0.0f;
	}
	Quaternion Quaternion::Inverse() const
	{
		return Quaternion();
	}
	Quaternion Quaternion::UnitInverse() const
	{
		return Quaternion();
	}
	Quaternion Quaternion::Exp() const
	{
		return Quaternion();
	}
	Quaternion Quaternion::Log() const
	{
		return Quaternion();
	}
	Vector3 Quaternion::operator*(const Vector3 & v) const
	{
		return Vector3();
	}
	float Quaternion::GetRoll(bool reprojectAxis) const
	{
		return 0.0f;
	}
	float Quaternion::GetPitch(bool reprojectAxis) const
	{
		return 0.0f;
	}
	float Quaternion::GetYaw(bool reprojectAxis) const
	{
		return 0.0f;
	}
	bool Quaternion::Equals(const Quaternion & rhs, float tolerance) const
	{
		return false;
	}
	Quaternion Quaternion::Slerp(float t, const Quaternion & p, const Quaternion & q, bool shortestPath)
	{
		return Quaternion();
	}
	Quaternion Quaternion::SlerpExtraSpins(float t, const Quaternion & p, const Quaternion & q, int extraSpins)
	{
		return Quaternion();
	}
	void Quaternion::Intermediate(const Quaternion & q0, const Quaternion & q1, const Quaternion & q2, Quaternion & a, Quaternion & b)
	{
	}
	Quaternion Quaternion::Squad(float t, const Quaternion & p, const Quaternion & a, const Quaternion & b, const Quaternion & q, bool shortestPath)
	{
		return Quaternion();
	}
	Quaternion Quaternion::NLerp(float t, const Quaternion & p, const Quaternion & q, bool shortestPath)
	{
		return Quaternion();
	}
}
