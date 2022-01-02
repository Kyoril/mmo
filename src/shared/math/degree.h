// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once


namespace mmo
{
	class Radian;

	/**
	 * \brief Contains a value in degrees and supports converting to angle and radian units.
	 */
	class Degree final
	{
	private:
		float m_value;

	public:
		/**
		 * \brief Default constructor which explicitly assigns a degree value.
		 * \param d The value in degrees to assign.
		 */
		explicit Degree(float d = 0)
			: m_value(d)
		{	
		}
		
		/**
		 * \brief Conversion operator from Radian value.
		 * \param r The radian value.
		 */
		Degree(const Radian& r);

	public:
		/**
		 * \brief Gets the internal value in degrees.
		 * \return Internal value in degrees.
		 */
		[[nodiscard]] float GetValueDegrees() const { return m_value; }
		/**
		 * \brief Gets the internal value converted into radians.
		 * \return Internal value converted into radians.
		 */
		[[nodiscard]] float GetValueRadians() const;
		/**
		 * \brief Gets the internal value converted into angle units.
		 * \return Internal value converted into angle units.
		 */
		[[nodiscard]] float GetValueAngleUnits() const { return m_value; }

	public:
		Degree& operator=(const float& f) { m_value = f; return *this; }
		Degree& operator=(const Degree& d) { m_value = d.m_value; return *this; }
		Degree& operator=(const Radian& r);

		const Degree& operator+() const { return *this; }
		Degree operator+(const Degree& d) const { return Degree(m_value + d.m_value); }
		Degree operator+(const Radian& r) const;
		Degree& operator+=(const Degree& d) { m_value += d.m_value; return *this; }
		Degree& operator+=(const Radian& r);
		Degree operator-() const { return Degree(-m_value); }
		Degree operator-(const Degree& d) const { return Degree(m_value - d.m_value); }
		Degree operator-(const Radian& r) const;
		Degree& operator-= (const Degree& d) { m_value -= d.m_value; return *this; }
		Degree& operator-=(const Radian& r);
		Degree operator*(float f) const { return Degree(m_value * f); }
		Degree operator*(const Degree& f) const { return Degree(m_value * f.m_value); }
		Degree& operator*=(float f) { m_value *= f; return *this; }
		Degree operator/(float f) const { return Degree(m_value / f); }
		Degree& operator/=(float f) { m_value /= f; return *this; }

		bool operator<(const Degree& d) const { return m_value < d.m_value; }
		bool operator<=(const Degree& d) const { return m_value <= d.m_value; }
		bool operator==(const Degree& d) const { return m_value == d.m_value; }
		bool operator!=(const Degree& d) const { return m_value != d.m_value; }
		bool operator>=(const Degree& d) const { return m_value >= d.m_value; }
		bool operator>(const Degree& d) const { return m_value > d.m_value; }
	};

	inline Degree operator * (float a, const Degree& b)
	{
		return Degree(a * b.GetValueRadians());
	}

	inline Degree operator / (float a, const Degree& b)
	{
		return Degree(a / b.GetValueRadians());
	}
}
