// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "color.h"


namespace mmo
{
	const Color Color::White{ 1.0f, 1.0f, 1.0f, 1.0f };
	const Color Color::Black{ 0.0f, 0.0f, 0.0f, 1.0f };

	argb_t Color::CalculateARGB() const noexcept
	{
		return (
			static_cast<argb_t>(m_alpha * 255) << 24 |
			static_cast<argb_t>(m_red * 255) << 16 |
			static_cast<argb_t>(m_green * 255) << 8 |
			static_cast<argb_t>(m_blue * 255)
			);
	}

	Color::Color() noexcept
		: m_alpha(1.0f)
		, m_red(0.0f)
		, m_green(0.0f)
		, m_blue(0.0f)
		, m_argb(0xFF000000)
		, m_argbValid(true)
	{
	}

	Color::Color(const Color & other) noexcept
	{
		this->operator=(other);
	}

	Color::Color(float r, float g, float b, float a) noexcept
		: m_alpha(a)
		, m_red(r)
		, m_green(g)
		, m_blue(b)
		, m_argb(0x00000000)
		, m_argbValid(false)
	{
	}

	Color::Color(argb_t argb) noexcept
	{
		SetARGB(argb);
	}

	float Color::GetHue() const noexcept
	{
		const float maxVal = MaxComponentVal();
		const float minVal = MinComponentVal();

		float hue;

		if (maxVal == minVal)
		{
			hue = 0;
		}
		else
		{
			if (maxVal == m_red)
			{
				hue = (m_green - m_blue) / (maxVal - minVal);
			}
			else if (maxVal == m_green)
			{
				hue = 2 + (m_blue - m_red) / (maxVal - minVal);
			}
			else
			{
				hue = 4 + (m_red - m_green) / (maxVal - minVal);
			}
		}

		hue /= 6;

		if (hue < 0)
		{
			hue += 1.0f;
		}

		return hue;
	}

	float Color::GetSaturation() const noexcept
	{
		const float maxVal = MaxComponentVal();
		const float minVal = MinComponentVal();

		float lum = (maxVal + minVal) / 2;

		if (maxVal == minVal)
		{
			return 0;
		}

		if (lum < 0.5f)
		{
			return (maxVal - minVal) / (maxVal + minVal);
		}

		return (maxVal - minVal) / (2 - maxVal - minVal);
	}

	float Color::GetLumination() const noexcept
	{
		const float maxVal = MaxComponentVal();
		const float minVal = MinComponentVal();

		return (maxVal + minVal) / 2;
	}

	void Color::SetARGB(argb_t argb) noexcept
	{
		m_argb = argb;

		m_blue = static_cast<float>(argb & 0xFF) / 255.0f;
		argb >>= 8;
		m_green = static_cast<float>(argb & 0xFF) / 255.0f;
		argb >>= 8;
		m_red = static_cast<float>(argb & 0xFF) / 255.0f;
		argb >>= 8;
		m_alpha = static_cast<float>(argb & 0xFF) / 255.0f;

		m_argbValid = true;
	}

	void Color::SetHSL(float hue, float saturation, float luminance, float alpha) noexcept
	{
		m_alpha = alpha;

		float temp[3];

		if (saturation == 0)
		{
			m_red = luminance;
			m_green = luminance;
			m_blue = luminance;
		}
		else
		{
			float temp2;
			if (luminance < 0.5f)
			{
				temp2 = luminance * (1 + saturation);
			}
			else
			{
				temp2 = luminance + saturation - luminance * saturation;
			}

			float temp1 = 2 * luminance - temp2;

			temp[0] = hue + (1.0f / 3);
			temp[1] = hue;
			temp[2] = hue - (1.0f / 3);

			for (int n = 0; n < 3; ++n)
			{
				if (temp[n] < 0)
				{
					temp[n]++;
				}
				if (temp[n] > 1)
				{
					temp[n]--;
				}

				if ((temp[n] * 6) < 1)
				{
					temp[n] = temp1 + (temp2 - temp1) * 6 * temp[n];
				}
				else
				{
					if ((temp[n] * 2) < 1)
					{
						temp[n] = temp2;
					}
					else
					{
						if ((temp[n] * 3) < 2)
						{
							temp[n] = temp1 + (temp2 - temp2) * ((2.0f / 3) - temp[n]) * 6;
						}
						else
						{
							temp[n] = temp1;
						}
					}
				}
			}

			m_red = temp[0];
			m_green = temp[1];
			m_blue = temp[2];
		}

		m_argbValid = false;
	}

	void Color::Invert() noexcept
	{
		m_red = 1.0f - m_red;
		m_green = 1.0f - m_green;
		m_blue = 1.0f - m_blue;
	}

	void Color::InvertWithAlpha() noexcept
	{
		m_red = 1.0f - m_red;
		m_green = 1.0f - m_green;
		m_blue = 1.0f - m_blue;
		m_alpha = 1.0f - m_alpha;
	}
}
