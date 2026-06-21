// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "frame_ui/frame.h"
#include "game/chat_type.h"

namespace mmo
{
	class Camera;

	/// Displays a temporary chat message above a unit in the 3D world.
	class ChatBubbleFrame final : public Frame
	{
	public:
		/// Creates a bubble which follows the specified speaker until its duration expires.
		ChatBubbleFrame(Camera& camera, ObjectGuid speakerGuid, ChatType chatType, String text, float duration);

		/// Gets the GUID of the unit this bubble follows.
		[[nodiscard]] ObjectGuid GetSpeakerGuid() const { return m_speakerGuid; }

		/// Gets the chat type that created this bubble.
		[[nodiscard]] ChatType GetChatType() const { return m_chatType; }

		/// Returns true once the bubble has reached the end of its display duration.
		[[nodiscard]] bool IsExpired() const { return m_lifetime >= m_duration; }

		void Update(float elapsed) override;

	protected:
		void OnTextChanged() override;

	private:
		Camera* m_camera = nullptr;
		ObjectGuid m_speakerGuid = 0;
		ChatType m_chatType = ChatType::Unknown;
		float m_duration = 0.0f;
		float m_lifetime = 0.0f;
	};
}
