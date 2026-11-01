// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <ostream>
#include <istream>
#include <iomanip>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>


namespace mmo
{
	typedef uint32 argb_t;


	/// This class represents a color and allows for conversion from and to several types and serialization.
	class Color final
	{
	public:
		static const Color White;
		static const Color Black;
		static const Color Red;
		static const Color Green;
		static const Color Blue;

	public:
		Color();
		Color(const Color& other);
		Color(float r, float g, float b, float a = 1.0f);
		Color(argb_t argb);

	public:

		inline argb_t GetARGB() const
		{
			if (!m_argbValid)
			{
				m_argb = CalculateARGB();
				m_argbValid = true;
			}
			return m_argb;
		}

		inline argb_t GetABGR() const
		{
			return CalculateABGR();
		}

		inline float GetAlpha() const { return m_alpha; }
		inline float GetRed() const { return m_red; }
		inline float GetGreen() const { return m_green; }
		inline float GetBlue() const { return m_blue; }
		inline void SetAlpha(float alpha)
		{
			m_argbValid = false;
			m_alpha = alpha;
		}
		inline void SetRed(float red)
		{
			m_argbValid = false;
			m_red = red;
		}
		inline void SetGreen(float green)
		{
			m_argbValid = false;
			m_green = green;
		}
		inline void SetBlue(float blue)
		{
			m_argbValid = false;
			m_blue = blue;
		}
		inline void Set(float red, float green, float blue, float alpha = 1.0f)
		{
			m_argbValid = false;
			m_red = red;
			m_green = green;
			m_blue = blue;
			m_alpha = alpha;
		}
		inline void SetRGB(float red, float green, float blue)
		{
			m_argbValid = false;
			m_red = red;
			m_green = green;
			m_blue = blue;
		}
		inline void SetRGB(const Color& col)
		{
			// Apply rgb components from other color
			m_red = col.m_red;
			m_green = col.m_green;
			m_blue = col.m_blue;

			// See if we can just use the other colors argb value.
			if (m_argbValid)
			{
				m_argbValid = col.m_argbValid;
				if (m_argbValid)
				{
					// Apply other colors rgb component but keep our own alpha component
					m_argb = (m_argb & 0xFF000000) | (col.m_argb & 0x00FFFFFF);
				}
			}
		}

	public:
		float GetHue() const;
		float GetSaturation() const;
		float GetLumination() const;
		void SetARGB(argb_t argb);
		void SetHSL(float hue, float saturation, float luminance, float alpha = 1.0f);
		void Invert();
		void InvertWithAlpha();

	public:
		inline operator float*() const
		{
			return &const_cast<Color*>(this)->m_red;
		}

		inline operator float*()
		{
			return &m_red;
		}

		inline Color& operator=(argb_t argb)
		{
			SetARGB(argb);
			return *this;
		}

		inline Color& operator=(const Color& color)
		{
			m_alpha = color.m_alpha;
			m_red = color.m_red;
			m_green = color.m_green;
			m_blue = color.m_blue;
			m_argb = color.m_argb;
			m_argbValid = color.m_argbValid;

			return *this;
		}
		inline Color& operator&=(argb_t argb)
		{
			SetARGB(GetARGB() & argb);
			return *this;
		}
		inline Color& operator&=(const Color& color)
		{
			SetARGB(GetARGB() & color.GetARGB());
			return *this;
		}
		inline Color& operator|=(argb_t argb)
		{
			SetARGB(GetARGB() | argb);
			return *this;
		}
		inline Color& operator|=(const Color& color)
		{
			SetARGB(GetARGB() | color.GetARGB());
			return *this;
		}
		inline Color& operator<<=(int32 val)
		{
			SetARGB(GetARGB() << val);
			return *this;
		}
		inline Color& operator>>=(int32 val)
		{
			SetARGB(GetARGB() >> val);
			return *this;
		}
		inline Color operator+(const Color& val) const
		{
			return Color(
				m_red + val.m_red,
				m_green + val.m_green,
				m_blue + val.m_blue,
				m_alpha + val.m_alpha
			);
		}
		inline Color operator-(const Color& val) const
		{
			return Color(
				m_red - val.m_red,
				m_green - val.m_green,
				m_blue - val.m_blue,
				m_alpha - val.m_alpha
			);
		}
		inline Color operator*(float scalar) const
		{
			return Color(
				m_red * scalar,
				m_green * scalar,
				m_blue * scalar,
				m_alpha * scalar
			);
		}

		inline Color& operator*=(const Color& color)
		{
			m_red *= color.m_red;
			m_green *= color.m_green;
			m_blue *= color.m_blue;
			m_alpha *= color.m_alpha;

			m_argbValid = false;

			return *this;
		}

		inline bool operator==(const Color& rhs) const
		{
			// Try to compare by argb value, which is uint32 instead of costly float comparison
			return
				std::fabs(m_red - rhs.m_red) <= FLT_EPSILON &&
				std::fabs(m_green - rhs.m_green) <= FLT_EPSILON &&
				std::fabs(m_blue - rhs.m_blue) <= FLT_EPSILON &&
				std::fabs(m_alpha - rhs.m_alpha) <= FLT_EPSILON;
		}

		inline bool operator!=(const Color& rhs) const
		{
			return !(*this == rhs);
		}

		/// Hex ARGB stream writer operator
		inline friend std::ostream& operator <<(std::ostream& s, const Color& val)
		{
			s.fill('0');
			s.width(8);
			s << std::hex;
			s << val.GetARGB();

			// Reset stream state to default state
			s << std::dec;
			s.fill(s.widen(' '));

			return s;
		}

		/// Hex ARGB stream reader operator
		inline friend std::istream& operator >>(std::istream& s, Color& val)
		{
			argb_t value = 0xFF000000;
			s >> std::hex >> value;
			val.SetARGB(value);
			s >> std::dec;
			return s;
		}
		/// Conversion operator.
		operator argb_t() const { return GetARGB(); }

		static argb_t CalculateARGB(uint8 alpha, uint8 red, uint8 green, uint8 blue)
		{
			return (
				static_cast<argb_t>(alpha) << 24 |
				static_cast<argb_t>(red) << 16 |
				static_cast<argb_t>(green) << 8 |
				static_cast<argb_t>(blue)
				);
		}

	private:
		argb_t CalculateARGB() const;
		argb_t CalculateABGR() const;

		inline float MaxComponentVal() const { return std::max(std::max(m_red, m_green), m_blue); }
		inline float MinComponentVal() const { return std::min(std::min(m_red, m_green), m_blue); }

	private:

		float m_alpha;
		float m_red;
		float m_green;
		float m_blue;
		mutable argb_t m_argb;
		mutable bool m_argbValid;
	};
}
