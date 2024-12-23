// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "degree.h"
#include "radian.h"
#include "math_utils.h"


namespace mmo
{
	Degree::Degree(const Radian& r)
		: m_value(r.GetValueDegrees())
	{
	}

	float Degree::GetValueRadians() const
	{
		return DegreesToRadians(m_value);
	}

	Degree& Degree::operator=(const Radian& r)
	{
		m_value = r.GetValueDegrees();
		return *this;
	}

	Degree Degree::operator+(const Radian& r) const
	{
		return Degree(m_value + r.GetValueDegrees());
	}

	Degree& Degree::operator+=(const Radian& r)
	{
		m_value += r.GetValueDegrees();
		return *this;
	}

	Degree Degree::operator-(const Radian& r) const
	{
		return Degree(m_value - r.GetValueDegrees());
	}

	Degree& Degree::operator-=(const Radian& r)
	{
		m_value -= r.GetValueDegrees();
		return *this;
	}
}
