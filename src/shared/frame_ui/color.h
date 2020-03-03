// Copyright (C) 2020, Robin Klimonow. All rights reserved.

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
		Color() noexcept;
		Color(const Color& other) noexcept;
		Color(float r, float g, float b, float a = 1.0f) noexcept;
		Color(argb_t argb) noexcept;

	public:

		inline argb_t GetARGB() const noexcept
		{
			if (!m_argbValid)
			{
				m_argb = CalculateARGB();
				m_argbValid = true;
			}
			return m_argb;
		}
		inline float GetAlpha() const noexcept { return m_alpha; }
		inline float GetRed() const noexcept { return m_red; }
		inline float GetGreen() const noexcept { return m_green; }
		inline float GetBlue() const noexcept { return m_blue; }
		inline void SetAlpha(float alpha) noexcept
		{
			m_argbValid = false;
			m_alpha = alpha;
		}
		inline void SetRed(float red) noexcept
		{
			m_argbValid = false;
			m_red = red;
		}
		inline void SetGreen(float green) noexcept
		{
			m_argbValid = false;
			m_green = green;
		}
		inline void SetBlue(float blue) noexcept
		{
			m_argbValid = false;
			m_blue = blue;
		}
		inline void Set(float red, float green, float blue, float alpha = 1.0f) noexcept
		{
			m_argbValid = false;
			m_red = red;
			m_green = green;
			m_blue = blue;
			m_alpha = alpha;
		}
		inline void SetRGB(float red, float green, float blue) noexcept
		{
			m_argbValid = false;
			m_red = red;
			m_green = green;
			m_blue = blue;
		}
		inline void SetRGB(const Color& col) noexcept
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
		float GetHue() const noexcept;
		float GetSaturation() const noexcept;
		float GetLumination() const noexcept;
		void SetARGB(argb_t argb) noexcept;
		void SetHSL(float hue, float saturation, float luminance, float alpha = 1.0f) noexcept;
		void Invert() noexcept;
		void InvertWithAlpha() noexcept;

	public:
		inline Color& operator=(argb_t argb) noexcept
		{
			SetARGB(argb);
			return *this;
		}
		inline Color& operator=(const Color& color) noexcept
		{
			m_alpha = color.m_alpha;
			m_red = color.m_red;
			m_green = color.m_green;
			m_blue = color.m_blue;
			m_argb = color.m_argb;
			m_argbValid = color.m_argbValid;

			return *this;
		}
		inline Color& operator&=(argb_t argb) noexcept
		{
			SetARGB(GetARGB() & argb);
			return *this;
		}
		inline Color& operator&=(const Color& color) noexcept
		{
			SetARGB(GetARGB() & color.GetARGB());
			return *this;
		}
		inline Color& operator|=(argb_t argb) noexcept
		{
			SetARGB(GetARGB() | argb);
			return *this;
		}
		inline Color& operator|=(const Color& color) noexcept
		{
			SetARGB(GetARGB() | color.GetARGB());
			return *this;
		}
		inline Color& operator<<=(int32 val) noexcept
		{
			SetARGB(GetARGB() << val);
			return *this;
		}
		inline Color& operator>>=(int32 val) noexcept
		{
			SetARGB(GetARGB() >> val);
			return *this;
		}
		inline Color operator+(const Color& val) const noexcept
		{
			return Color(
				m_red + val.m_red,
				m_green + val.m_green,
				m_blue + val.m_blue,
				m_alpha + val.m_alpha
			);
		}
		inline Color operator-(const Color& val) const noexcept
		{
			return Color(
				m_red - val.m_red,
				m_green - val.m_green,
				m_blue - val.m_blue,
				m_alpha - val.m_alpha
			);
		}
		inline Color operator*(float scalar) const noexcept
		{
			return Color(
				m_red * scalar,
				m_green * scalar,
				m_blue * scalar,
				m_alpha * scalar
			);
		}
		inline Color& operator*=(const Color& color) noexcept
		{
			m_red *= color.m_red;
			m_green *= color.m_green;
			m_blue *= color.m_blue;
			m_alpha *= color.m_alpha;

			m_argbValid = false;

			return *this;
		}
		inline bool operator==(const Color& rhs) const noexcept
		{
			// Try to compare by argb value, which is uint32 instead of costly float comparison
			return
				std::fabs(m_red - rhs.m_red) <= FLT_EPSILON &&
				std::fabs(m_green - rhs.m_green) <= FLT_EPSILON &&
				std::fabs(m_blue - rhs.m_blue) <= FLT_EPSILON &&
				std::fabs(m_alpha - rhs.m_alpha) <= FLT_EPSILON;
		}
		inline bool operator!=(const Color& rhs) const noexcept
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
		operator argb_t() const noexcept { return GetARGB(); }

		static argb_t CalculateARGB(uint8 alpha, uint8 red, uint8 green, uint8 blue) noexcept
		{
			return (
				static_cast<argb_t>(alpha) << 24 |
				static_cast<argb_t>(red) << 16 |
				static_cast<argb_t>(green) << 8 |
				static_cast<argb_t>(blue)
				);
		}

	private:
		argb_t CalculateARGB() const noexcept;

		inline float MaxComponentVal() const noexcept { return std::max(std::max(m_red, m_green), m_blue); }
		inline float MinComponentVal() const noexcept { return std::min(std::min(m_red, m_green), m_blue); }

	private:

		float m_alpha;
		float m_red;
		float m_green;
		float m_blue;
		mutable argb_t m_argb;
		mutable bool m_argbValid;
	};
}
