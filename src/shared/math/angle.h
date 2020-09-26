#pragma once


namespace mmo
{
	class Radian;
	class Degree;


	/**
	 * \brief Contains possible default unit for angle units.
	 */
	enum class AngleUnit
	{
		/**
		 * \brief Angle units are stored as degree units internally.
		 */
		Degree,
		/**
		 * \brief Angle units are stored as radian units internally.
		 */
		Radian
	};
	
	
	/**
	 * \brief This class represents a value in angle units. Values in angle units will be automatically
	 * converted to either Degree or Radian values where needed.
	 */
	class Angle final
	{
	private:
		static AngleUnit s_angleUnit;

	public:
		static void SetAngleUnit(AngleUnit unit) noexcept { s_angleUnit = unit; }
		static AngleUnit GetAngleUnit() noexcept { return s_angleUnit; }

		/**
		 * \brief Converts a given angle unit value to a degree value.
		 * \param value The value in angle units.
		 * \return Value in degrees.
		 */
		static float AngleUnitsToDegrees(float value);
		/**
		 * \brief Converts a given angle unit value to a radian value.
		 * \param value The value in angle units.
		 * \return Value in radians.
		 */
		static float AngleUnitsToRadians(float value);
		
	private:
		// The value in angle units.
		float m_value;
		
	public:
		/**
		 * \brief Default constructor which assigns an angle unit value.
		 * \param angle The value in angle units.
		 */
		explicit Angle(float angle)
			: m_value(angle)
		{
		}

	public:
		/**
		 * \brief Cast operator to Radian class.
		 */
		operator Radian() const;
		/**
		 * \brief Cast operator to Degree class.
		 */
		operator Degree() const;
	};
}
