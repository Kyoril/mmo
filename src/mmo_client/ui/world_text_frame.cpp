// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_text_frame.h"

#include "frame_ui/font_mgr.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/geometry_buffer.h"
#include "math/clamp.h"
#include "math/constants.h"
#include "scene_graph/camera.h"

#include "base/random.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		float easeOutCubic(const float progress)
		{
			const float inverse = 1.0f - Clamp(progress, 0.0f, 1.0f);
			return 1.0f - inverse * inverse * inverse;
		}

		float smoothStep(const float progress)
		{
			const float clamped = Clamp(progress, 0.0f, 1.0f);
			return clamped * clamped * (3.0f - 2.0f * clamped);
		}
	}

	WorldTextFrame::WorldTextFrame(Camera& camera, const Vector3& position, const float duration, const Color& color, const WorldTextAnimation animation)
		: Frame("WorldTextFrame", "")
		, m_camera(&camera)
		, m_worldPosition(position)
		, m_duration(duration)
		, m_lifetime(0.0f)
		, m_textColor(color)
		, m_animation(animation)
		, m_textScale(1.0f)
		, m_baseTextWidth(0.0f)
		, m_baseTextHeight(0.0f)
		, m_horizontalDirection(1.0f)
	{
		// TODO: Again, don't hardcode this here as it is really unflexible. Instead we would want to configure this somewhere
		// in the game data.
		if (m_animation == WorldTextAnimation::Critical)
		{
			std::uniform_real_distribution offsetX(-28.0f, 28.0f);
			std::uniform_real_distribution offsetY(68.0f, 104.0f);
			std::uniform_real_distribution direction(-1.0f, 1.0f);

			m_offset = Point(offsetX(RandomGenerator), offsetY(RandomGenerator));
			m_horizontalDirection = direction(RandomGenerator) < 0.0f ? -1.0f : 1.0f;
			m_font = FontManager::Get().CreateOrRetrieve("Fonts/SKURRI.TTF", 60.0f, 7.0f);
		}
		else
		{
			std::uniform_real_distribution offsetX(-50.0f, 50.0f);
			std::uniform_real_distribution offsetY(0.0f, 100.0f);

			m_offset = Point(offsetX(RandomGenerator), 40.0f + offsetY(RandomGenerator));
			m_font = FontManager::Get().CreateOrRetrieve("Fonts/SKURRI.TTF", 48.0f, 5.0f);
		}
	}

	void WorldTextFrame::OnTextChanged()
	{
		Frame::OnTextChanged();

		m_baseTextWidth = m_font->GetTextWidth(m_text);
		m_baseTextHeight = m_font->GetHeight();
		m_pixelSize.width = m_baseTextWidth * m_textScale;
		m_pixelSize.height = m_baseTextHeight * m_textScale;
	}

	void WorldTextFrame::Update(float elapsed)
	{
		m_lifetime += elapsed;

		// No more expensive updates for expired frames
		if (IsExpired())
		{
			return;
		}

		const float inverseUiScale = 1.0f / FrameManager::Get().GetUIScale().y;

		int32 width, height;
		GraphicsDevice::Get().GetViewport(nullptr, nullptr, &width, &height);

		// Update position
		if (m_camera)
		{
			// Calculate normalized screen position (0.0f - 1.0f)
			m_camera->GetNormalizedScreenPosition(m_worldPosition, m_position.x, m_position.y);

			// Get actual screen position by multiplying normalized position with screen size
			m_position.x *= static_cast<float>(width);
			m_position.y *= static_cast<float>(height);

			float horizontalMovement = 0.0f;
			float verticalMovement = m_lifetime * 50.0f;

			if (m_animation == WorldTextAnimation::Critical)
			{
				constexpr float growDuration = 0.16f;
				constexpr float settleDuration = 0.30f;
				constexpr float peakScale = 1.58f;
				constexpr float settledScale = 1.15f;

				if (m_lifetime < growDuration)
				{
					m_textScale = 0.62f + (peakScale - 0.62f) * easeOutCubic(m_lifetime / growDuration);
				}
				else if (m_lifetime < growDuration + settleDuration)
				{
					const float settleProgress = (m_lifetime - growDuration) / settleDuration;
					m_textScale = peakScale + (settledScale - peakScale) * smoothStep(settleProgress);
				}
				else
				{
					const float bounceProgress = Clamp((m_lifetime - growDuration - settleDuration) / 0.42f, 0.0f, 1.0f);
					const float bounce = std::sin(bounceProgress * TwoPi * 1.5f) * (1.0f - bounceProgress) * 0.10f;
					m_textScale = settledScale + bounce;
				}

				const float burstProgress = easeOutCubic(Clamp(m_lifetime / 0.85f, 0.0f, 1.0f));
				const float wobbleStrength = 1.0f - Clamp(m_lifetime / 1.15f, 0.0f, 1.0f);
				horizontalMovement = m_horizontalDirection * 72.0f * burstProgress;
				horizontalMovement += std::sin(m_lifetime * 18.0f) * 10.0f * wobbleStrength;

				const float upwardBurst = easeOutCubic(Clamp(m_lifetime / 0.52f, 0.0f, 1.0f)) * 112.0f;
				verticalMovement = upwardBurst + std::max(0.0f, m_lifetime - 0.52f) * 34.0f;

				const float impactJitter = 1.0f - Clamp(m_lifetime / 0.24f, 0.0f, 1.0f);
				horizontalMovement += std::sin(m_lifetime * 84.0f) * 7.0f * impactJitter;
				verticalMovement += std::cos(m_lifetime * 70.0f) * 5.0f * impactJitter;

				const float fadeStart = std::max(0.0f, m_duration - 1.0f);
				const float fadeProgress = (m_lifetime - fadeStart) / std::max(0.001f, m_duration - fadeStart);
				SetOpacity(1.0f - smoothStep(fadeProgress));
			}
			else
			{
				m_textScale = 1.0f;
				SetOpacity(1.0f - ((m_lifetime - 0.5f) / (m_duration - 0.5f)));
			}

			m_pixelSize.width = m_baseTextWidth * m_textScale;
			m_pixelSize.height = m_baseTextHeight * m_textScale;

			// Center around position
			m_position.x -= m_pixelSize.width * 0.5f;
			m_position.y -= m_pixelSize.height * 0.5f;
			m_position -= m_offset;
			m_position.x += horizontalMovement;
			m_position.y -= verticalMovement;

			m_position.x *= inverseUiScale;
			m_position.y *= inverseUiScale;

			Invalidate();
		}

		Frame::Update(elapsed);
	}

	void WorldTextFrame::PopulateGeometryBuffer()
	{
		if (!m_font)
		{
			return;
		}

		const float uiScale = FrameManager::Get().GetUIScale().y;
		const Rect frameRect = GetAbsoluteFrameRect();
		const Point textPosition = frameRect.GetPosition();
		const float opacity = GetOpacity(true);

		if (m_animation == WorldTextAnimation::Critical)
		{
			const float impact = 1.0f - Clamp(m_lifetime / 0.46f, 0.0f, 1.0f);
			const float accentScale = m_textScale * (1.0f + impact * 0.22f);
			const float accentWidth = m_baseTextWidth * accentScale * uiScale;
			const float accentHeight = m_baseTextHeight * accentScale * uiScale;
			const Point accentPosition(
				textPosition.x - (accentWidth - frameRect.GetWidth()) * 0.5f,
				textPosition.y - (accentHeight - frameRect.GetHeight()) * 0.5f);

			Color accentColor(1.0f, 0.58f, 0.04f, impact * opacity * 0.82f);
			m_font->DrawText(m_text, accentPosition, GetGeometryBuffer(), accentScale * uiScale, accentColor.GetARGB());
		}

		const float flash = m_animation == WorldTextAnimation::Critical
			? 1.0f - Clamp(m_lifetime / 0.24f, 0.0f, 1.0f)
			: 0.0f;
		Color textColor(
			m_textColor.GetRed() + (1.0f - m_textColor.GetRed()) * flash * 0.72f,
			m_textColor.GetGreen() + (1.0f - m_textColor.GetGreen()) * flash * 0.72f,
			m_textColor.GetBlue() + (1.0f - m_textColor.GetBlue()) * flash * 0.72f,
			m_textColor.GetAlpha() * opacity);

		m_font->DrawText(m_text, textPosition, GetGeometryBuffer(), m_textScale * uiScale, textColor.GetARGB());
	}
}
