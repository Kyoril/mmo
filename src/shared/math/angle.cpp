// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "angle.h"
#include "degree.h"
#include "radian.h"
#include "math_utils.h"


namespace mmo
{
	AngleUnit Angle::s_angleUnit = AngleUnit::Degree;

	
	float Angle::AngleUnitsToDegrees(float value)
	{
		if (s_angleUnit == AngleUnit::Degree)
			return value;

		return RadiansToDegrees(value);
	}

	float Angle::AngleUnitsToRadians(float value)
	{
		if (s_angleUnit == AngleUnit::Radian)
			return value;

		return DegreesToRadians(value);
	}

	Angle::operator Radian() const
	{
		return Radian(AngleUnitsToRadians(m_value));
	}

	Angle::operator Degree() const
	{
		return Degree(AngleUnitsToDegrees(m_value));
	}
}
