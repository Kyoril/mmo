// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chat_bubble_frame.h"

#include "frame_ui/border_component.h"
#include "frame_ui/font_mgr.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/text_component.h"
#include "game_client/game_unit_c.h"
#include "game_client/object_mgr.h"
#include "graphics/graphics_device.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		constexpr float BubbleMinWidth = 180.0f;
		constexpr float BubbleMaxWidth = 420.0f;
		constexpr float BubbleHorizontalPadding = 36.0f;
		constexpr float BubbleVerticalPadding = 24.0f;
		constexpr float BubbleScreenMargin = 12.0f;
		constexpr float BubbleWorldOffset = 0.2f;
		constexpr float BubbleFadeDuration = 0.75f;
	}

	ChatBubbleFrame::ChatBubbleFrame(Camera& camera, const ObjectGuid speakerGuid, const ChatType chatType, String text, const float duration)
		: Frame("ChatBubbleFrame", "")
		, m_camera(&camera)
		, m_speakerGuid(speakerGuid)
		, m_chatType(chatType)
		, m_duration(duration)
	{
		m_font = FontManager::Get().CreateOrRetrieve("Fonts/FRIZQT__.TTF", 24.0f, 0.0f, 1.0f, 1.0f);

		Color textColor = Color::White;
		argb_t borderTint = 0xf0202020;
		switch (m_chatType)
		{
		case ChatType::Yell:
		case ChatType::UnitYell:
			textColor = Color(1.0f, 0.72f, 0.72f, 1.0f);
			borderTint = 0xf0401818;
			break;
		case ChatType::Group:
			textColor = Color(0.76f, 0.82f, 1.0f, 1.0f);
			borderTint = 0xf0182040;
			break;
		default:
			break;
		}

		ImagerySection backgroundSection("Background");
		auto background = std::make_unique<BorderComponent>(*this, "Interface/GameUI/fg4_borders_insetBlackSmall.htex", 11.0f);
		background->SetTint(borderTint);
		backgroundSection.AddComponent(std::move(background));

		ImagerySection textSection("Text");
		auto textComponent = std::make_unique<TextComponent>(*this);
		textComponent->SetColor(textColor);
		textComponent->SetHorizontalAlignment(HorizontalAlignment::Center);
		textComponent->SetVerticalAlignment(VerticalAlignment::Center);
		textComponent->SetInset(Rect(
			BubbleHorizontalPadding * 0.5f,
			BubbleVerticalPadding * 0.5f,
			BubbleHorizontalPadding * 0.5f,
			BubbleVerticalPadding * 0.5f));
		textSection.AddComponent(std::move(textComponent));

		FrameLayer backgroundLayer;
		backgroundLayer.AddSection(*AddImagerySection(backgroundSection));
		FrameLayer textLayer;
		textLayer.AddSection(*AddImagerySection(textSection));

		StateImagery enabledState("Enabled");
		enabledState.AddLayer(backgroundLayer);
		enabledState.AddLayer(textLayer);
		AddStateImagery(enabledState);
		SetRenderer("DefaultRenderer");
		SetText(std::move(text));
	}

	void ChatBubbleFrame::OnTextChanged()
	{
		Frame::OnTextChanged();

		if (!m_font)
		{
			return;
		}

		const float width = std::clamp(
			m_font->GetTextWidth(m_text) + BubbleHorizontalPadding,
			BubbleMinWidth,
			BubbleMaxWidth);
		const Rect textArea(0.0f, 0.0f, width - BubbleHorizontalPadding, 10000.0f);
		const int lineCount = std::max(1, m_font->GetLineCount(m_text, textArea));
		const float height = m_font->GetHeight() * static_cast<float>(lineCount) + BubbleVerticalPadding;
		SetSize(width, height);
	}

	void ChatBubbleFrame::Update(const float elapsed)
	{
		m_lifetime += elapsed;
		if (IsExpired())
		{
			SetVisible(false);
			return;
		}

		const auto speaker = ObjectMgr::Get<GameUnitC>(m_speakerGuid);
		if (!speaker || !m_camera)
		{
			m_lifetime = m_duration;
			SetVisible(false);
			return;
		}

		Vector3 worldPosition = speaker->GetPosition() + Vector3::UnitY * 2.0f;
		if (const Entity* entity = speaker->GetEntity())
		{
			const AABB& bounds = entity->GetWorldBoundingBox(true);
			if (!bounds.IsNull())
			{
				worldPosition.y = bounds.max.y + BubbleWorldOffset;
			}
		}

		const Vector3 cameraToBubble = worldPosition - m_camera->GetDerivedPosition();
		if (cameraToBubble.Dot(m_camera->GetDerivedDirection()) <= 0.0f)
		{
			SetVisible(false);
			return;
		}

		float normalizedX = 0.0f;
		float normalizedY = 0.0f;
		m_camera->GetNormalizedScreenPosition(worldPosition, normalizedX, normalizedY);
		if (normalizedX < 0.0f || normalizedX > 1.0f || normalizedY < 0.0f || normalizedY > 1.0f)
		{
			SetVisible(false);
			return;
		}

		int32 viewportWidth = 0;
		int32 viewportHeight = 0;
		GraphicsDevice::Get().GetViewport(nullptr, nullptr, &viewportWidth, &viewportHeight);

		const float inverseUiScale = 1.0f / FrameManager::Get().GetUIScale().y;
		const float logicalWidth = static_cast<float>(viewportWidth) * inverseUiScale;
		const float logicalHeight = static_cast<float>(viewportHeight) * inverseUiScale;
		Point position(
			normalizedX * logicalWidth - GetWidth() * 0.5f,
			normalizedY * logicalHeight - GetHeight() - BubbleScreenMargin);
		position.x = std::clamp(position.x, BubbleScreenMargin, std::max(BubbleScreenMargin, logicalWidth - GetWidth() - BubbleScreenMargin));
		position.y = std::clamp(position.y, BubbleScreenMargin, std::max(BubbleScreenMargin, logicalHeight - GetHeight() - BubbleScreenMargin));
		SetPosition(position);

		const float remaining = m_duration - m_lifetime;
		SetOpacity(remaining < BubbleFadeDuration ? remaining / BubbleFadeDuration : 1.0f);
		SetVisible(true);
		Frame::Update(elapsed);
	}
}
