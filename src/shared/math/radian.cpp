// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "radian.h"
#include "degree.h"
#include "math_utils.h"


namespace mmo
{
	Radian::Radian(const Degree& degree)
		: m_value(degree.GetValueRadians())
	{
	}

	Radian& Radian::operator=(const Degree& d)
	{
		m_value = d.GetValueRadians();
		return *this;
	}

	float Radian::GetValueDegrees() const
	{
		return RadiansToDegrees(m_value);
	}

	float Radian::GetValueAngleUnits() const
	{
		return RadiansToDegrees(m_value);
	}

	Radian Radian::operator+(const Degree& d) const
	{
		return Radian(m_value + d.GetValueRadians());
	}

	Radian& Radian::operator+=(const Degree& d)
	{
		m_value += d.GetValueRadians();
		return *this;
	}

	Radian Radian::operator-(const Degree& d) const
	{
		return Radian(m_value - d.GetValueRadians());
	}

	Radian& Radian::operator-=(const Degree& d)
	{
		m_value -= d.GetValueRadians();
		return *this;
	}
}
