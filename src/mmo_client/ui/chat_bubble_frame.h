// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "frame_ui/frame.h"
#include "game/chat_type.h"

namespace mmo
{
	class Camera;

	/// Displays a temporary chat message above a unit in the 3D world.
	///
	/// The visual look (background texture, tail, font, padding colors) is defined in
	/// data/client/Interface/GameUI/ChatBubble.xml and copied onto the frame at
	/// construction time, so it can be tweaked without code changes.
	class ChatBubbleFrame final : public Frame
	{
	public:
		/// Creates a bubble which follows the specified speaker until its duration expires.
		/// @param name Unique frame name (also used to name the child tail frame).
		ChatBubbleFrame(const String& name, Camera& camera, ObjectGuid speakerGuid, ChatType chatType, String text, float duration);

		/// Gets the GUID of the unit this bubble follows.
		[[nodiscard]] ObjectGuid GetSpeakerGuid() const { return m_speakerGuid; }

		/// Gets the chat type that created this bubble.
		[[nodiscard]] ChatType GetChatType() const { return m_chatType; }

		/// Returns true once the bubble has reached the end of its display duration.
		[[nodiscard]] bool IsExpired() const { return m_lifetime >= m_duration; }

		/// Advances the bubble's lifetime and repositions it over its speaker.
		///
		/// This is driven manually by WorldState rather than the frame tree's Update so that
		/// the bubble keeps reacting even while it is temporarily hidden (e.g. off screen or
		/// behind the camera).
		void Animate(float elapsed);

	protected:
		void OnTextChanged() override;

	private:
		/// Copies the matching XML template imagery onto this frame and applies the
		/// per chat-type text color.
		void ApplyTemplate();

		/// Creates and attaches the downward-pointing tail child frame.
		void CreateTail();

		Camera* m_camera = nullptr;
		ObjectGuid m_speakerGuid = 0;
		ChatType m_chatType = ChatType::Unknown;
		float m_duration = 0.0f;
		float m_lifetime = 0.0f;

		/// The little tail that points from the bubble towards the speaker.
		FramePtr m_tail;
	};
}
