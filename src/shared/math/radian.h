// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

namespace mmo
{
	class Degree;

	
	/**
	 * \brief Contains a value in radians and supports conversion from and to Degrees and Angle Units.
	 */
	class Radian final
	{
	private:
		float m_value;

	public:
		/**
		 * \brief Default constructor which initializes the class instance to a specific radian value.
		 * \param v The radian value to initialize with. Defaults to 0.0f.
		 */
		explicit Radian(float v = 0.0f)
			: m_value(v)
		{
		}

		/**
		 * \brief Conversion constructor which converts a Degree value into Radians.
		 * \param degree The degree value.
		 */
		Radian(const Degree& degree);

	public:
		/**
		 * \brief Gets the internal value converted into degrees.
		 * \return Internal value converted into degrees.
		 */
		[[nodiscard]] float GetValueDegrees() const;
		/**
		 * \brief Gets the internal value in radians.
		 * \return Internal value in radians.
		 */
		[[nodiscard]] float GetValueRadians() const { return m_value; }
		/**
		 * \brief Gets the internal value converted into angle units.
		 * \return Internal value converted into angle units.
		 */
		[[nodiscard]] float GetValueAngleUnits() const;

	public:
		Radian& operator=(float v) { m_value = v; return *this; }
		Radian& operator=(const Radian& r) { m_value = r.m_value; return *this; }
		Radian& operator=(const Degree& d);

		const Radian& operator+() const { return *this; }
		Radian operator+(const Radian& r) const { return Radian(m_value + r.m_value); }
		Radian operator+(const Degree& d) const;
		Radian& operator+=(const Radian& r) { m_value += r.m_value; return *this; }
		Radian& operator+=(const Degree& d);
		Radian operator-() const { return Radian(-m_value); }
		Radian operator-(const Radian& r) const { return Radian(m_value - r.m_value); }
		Radian operator-(const Degree& d) const;
		Radian& operator-=(const Radian& r) { m_value -= r.m_value; return *this; }
		Radian& operator-=(const Degree& d);
		Radian operator*(float f) const { return Radian(m_value * f); }
		Radian operator*(const Radian& f) const { return Radian(m_value * f.m_value); }
		Radian& operator*=(float f) { m_value *= f; return *this; }
		Radian operator/(float f) const { return Radian(m_value / f); }
		Radian& operator/=(float f) { m_value /= f; return *this; }

		bool operator<(const Radian& r) const { return m_value < r.m_value; }
		bool operator<=(const Radian& r) const { return m_value <= r.m_value; }
		bool operator==(const Radian& r) const { return m_value == r.m_value; }
		bool operator!=(const Radian& r) const { return m_value != r.m_value; }
		bool operator>=(const Radian& r) const { return m_value >= r.m_value; }
		bool operator>(const Radian& r) const { return m_value > r.m_value; }
	};


	inline Radian operator * (float a, const Radian& b)
	{
		return Radian(a * b.GetValueRadians());
	}

	inline Radian operator / (float a, const Radian& b)
	{
		return Radian(a / b.GetValueRadians());
	}
}
