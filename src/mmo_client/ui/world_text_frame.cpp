// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_text_frame.h"

#include "frame_ui/font_mgr.h"
#include "frame_ui/frame_mgr.h"
#include "scene_graph/camera.h"

#include "base/random.h"

namespace mmo
{
	WorldTextFrame::WorldTextFrame(Camera& camera, const Vector3& position, float duration)
		: Frame("WorldTextFrame", "")
	{
		m_camera = &camera;
		m_worldPosition = position;
		m_duration = duration;
		m_lifetime = 0.0f;

		std::uniform_real_distribution distX(-50.0f, 50.0f);
		std::uniform_real_distribution distY(0.0f, 100.0f);
		m_offset = Point(distX(RandomGenerator), 40.0f + distY(RandomGenerator));

		// TODO: Again, don't hardcode this here as it is really unflexible. Instead we would want to configure this somewhere
		// in the game data.
		m_font = FontManager::Get().CreateOrRetrieve("Fonts/SKURRI.TTF", 48.0f, 5.0f);
	}

	void WorldTextFrame::OnTextChanged()
	{
		Frame::OnTextChanged();

		m_pixelSize.width = m_font->GetTextWidth(m_text);
		m_pixelSize.height = m_font->GetHeight();
	}

	void WorldTextFrame::Update(float elapsed)
	{
		m_lifetime += elapsed;

		// No more expensive updates for expired frames
		if (IsExpired())
		{
			return;
		}

		const float uiScale = 1.0f / FrameManager::Get().GetUIScale().y;

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

			// Center around position
			m_position.x -= m_pixelSize.width * 0.5f;
			m_position.y -= m_pixelSize.height * 0.5f;
			m_position -= m_offset;

			SetOpacity(1.0f - ((m_lifetime - 0.5f) / (m_duration - 0.5f)));

			m_position.y -= m_lifetime * 50.0f;

			m_position.x *= uiScale;
			m_position.y *= uiScale;

			Invalidate();
		}

		Frame::Update(elapsed);
	}
}
