// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chat_bubble_frame.h"

#include "frame_ui/frame_mgr.h"
#include "frame_ui/property.h"
#include "game_client/game_unit_c.h"
#include "game_client/object_mgr.h"
#include "graphics/graphics_device.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace mmo
{
	namespace
	{
		constexpr float BubbleMinWidth = 180.0f;
		constexpr float BubbleMaxWidth = 420.0f;
		// Total padding added around the text. Must match the text component inset
		// defined in ChatBubble.xml (left + right and top + bottom respectively).
		constexpr float BubbleHorizontalPadding = 48.0f;
		constexpr float BubbleVerticalPadding = 28.0f;
		constexpr float BubbleScreenMargin = 10.0f;
		// Vertical room reserved below the bubble body for the tail so its tip points at the speaker.
		constexpr float BubbleTailHeight = 20.0f;
		constexpr float BubbleWorldOffset = 0.2f;
		constexpr float BubbleFadeDuration = 0.75f;

		const std::string ChatBubbleTemplateName("ChatBubbleTemplate");
		const std::string ChatBubbleTailTemplateName("ChatBubbleTailTemplate");

		/// Formats an ARGB color as the 8-digit hex string expected by frame properties.
		std::string ToHexColor(const argb_t argb)
		{
			std::ostringstream stream;
			stream << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << argb;
			return stream.str();
		}

		/// Returns the text color used for a given chat type.
		argb_t GetTextColorForChatType(const ChatType chatType)
		{
			switch (chatType)
			{
			case ChatType::Yell:
			case ChatType::UnitYell:
				return 0xffFF4040;	// dark red
			case ChatType::Group:
			case ChatType::Raid:
				return 0xffAAAAFF;	// dark blue
			default:
				return 0xff1c1a20;	// near-black
			}
		}
	}

	ChatBubbleFrame::ChatBubbleFrame(const String& name, Camera& camera, const ObjectGuid speakerGuid, const ChatType chatType, String text, const float duration)
		: Frame("ChatBubbleFrame", name)
		, m_camera(&camera)
		, m_speakerGuid(speakerGuid)
		, m_chatType(chatType)
		, m_duration(duration)
	{
		ApplyTemplate();
		CreateTail();

		// Bubbles relate to the game world, not the UI: keep them from intercepting mouse
		// input (clicks fall through to the world) and from being hover/hit targets.
		SetClickable(false);
		SetEnabled(false);

		SetText(std::move(text));
	}

	void ChatBubbleFrame::ApplyTemplate()
	{
		const FramePtr templateFrame = FrameManager::Get().Find(ChatBubbleTemplateName);
		if (!templateFrame)
		{
			ELOG("Missing chat bubble template '" << ChatBubbleTemplateName << "' - is ChatBubble.xml loaded?");
			return;
		}

		// Copy the whole look (background border, text imagery, font, renderer) from the XML template.
		templateFrame->Copy(*this);

		// Apply the per chat-type text color through the bound 'TextColor' property.
		if (Property* textColor = GetProperty("TextColor"))
		{
			textColor->Set(ToHexColor(GetTextColorForChatType(m_chatType)));
		}
	}

	void ChatBubbleFrame::CreateTail()
	{
		const FramePtr templateFrame = FrameManager::Get().Find(ChatBubbleTailTemplateName);
		if (!templateFrame)
		{
			ELOG("Missing chat bubble tail template '" << ChatBubbleTailTemplateName << "' - is ChatBubble.xml loaded?");
			return;
		}

		// Create the tail directly (not via FrameManager) so it is owned solely by this bubble
		// and destroyed together with it, instead of leaking into the global frame registry.
		m_tail = std::make_shared<Frame>("Frame", GetName() + "_Tail");
		templateFrame->Copy(*m_tail);

		m_tail->SetClickable(false);
		m_tail->SetEnabled(false);

		// Centered horizontally and hanging just below the body. The small upward overlap
		// lets the tail cover the body's bottom border so the outline flows into the point.
		m_tail->SetAnchor(anchor_point::HorizontalCenter, anchor_point::HorizontalCenter, nullptr, 0.0f);
		m_tail->SetAnchor(anchor_point::Top, anchor_point::Bottom, nullptr, -3.0f);

		AddChild(m_tail);
	}

	void ChatBubbleFrame::OnTextChanged()
	{
		Frame::OnTextChanged();

		const FontPtr font = GetFont();
		if (!font)
		{
			return;
		}

		const float width = std::clamp(
			font->GetTextWidth(m_text) + BubbleHorizontalPadding,
			BubbleMinWidth,
			BubbleMaxWidth);
		const Rect textArea(0.0f, 0.0f, width - BubbleHorizontalPadding, 10000.0f);
		const int lineCount = std::max(1, font->GetLineCount(m_text, textArea));
		const float height = font->GetHeight() * static_cast<float>(lineCount) + BubbleVerticalPadding;
		SetSize(width, height);
	}

	void ChatBubbleFrame::Animate(const float elapsed)
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
			normalizedY * logicalHeight - GetHeight() - BubbleScreenMargin - BubbleTailHeight);
		position.x = std::clamp(position.x, BubbleScreenMargin, std::max(BubbleScreenMargin, logicalWidth - GetWidth() - BubbleScreenMargin));
		position.y = std::clamp(position.y, BubbleScreenMargin, std::max(BubbleScreenMargin, logicalHeight - GetHeight() - BubbleScreenMargin));
		SetPosition(position);

		// Re-layout the bubble subtree (including the anchored tail) for the new position.
		Invalidate();

		const float remaining = m_duration - m_lifetime;
		SetOpacity(remaining < BubbleFadeDuration ? remaining / BubbleFadeDuration : 1.0f);
		SetVisible(true);
	}
}
